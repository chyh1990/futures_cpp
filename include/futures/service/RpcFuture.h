#pragma once

#include <futures/Exception.h>
#include <futures/io/IoFuture.h>
#include <futures/service/ServerDispatcher.h>

namespace futures {
namespace service {

template <typename ReadStream, typename WriteSink>
class RpcFuture : public FutureBase<RpcFuture<ReadStream, WriteSink>, folly::Unit> {
public:
    using Item = folly::Unit;

    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    using DispatchType = IDispatcher<Req, Resp>;

    Poll<Item> poll() override {
        FUTURES_DLOG(INFO) << "Pipeline::tick";
        // 1) process inbound frames
        while (!read_closed_) {
            FUTURES_DLOG(INFO) << "reading frames";
            auto r = stream_.poll();
            if (r.hasException()) {
                FUTURES_DLOG(ERROR) << "bad frames: " << r.exception().what();
                dispatcher_->dispatchErr(r.exception());
                return Poll<Item>(r.exception());
            }
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                if (v->hasValue()) {
                    try {
                        dispatcher_->dispatch(std::move(v).value().value());
                    } catch (std::exception &e) {
                        FUTURES_DLOG(INFO) << "dispatcher exception: " << e.what();
                        transport_->shutdownWrite();
                        return Poll<Item>(folly::exception_wrapper(std::current_exception(), e));
                    }
                } else {
                    // read-side closed
                    read_closed_ = true;
                    FUTURES_DLOG(INFO) << "read side closed";
                }
            } else {
                break;
            }
        }

        // 2) process outbound frame
        while (!write_closed_) {
            auto r = dispatcher_->poll();
            if (r.hasException()) {
                FUTURES_DLOG(ERROR) << "dispatch poll error: " << r.exception().what();
                return Poll<Item>(r.exception());
            }
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                if (v->hasValue()) {
                    auto r = sink_.startSend(std::move(v->value()));
                    if (r.hasException()) {
                        FUTURES_DLOG(ERROR) << "encode frame error: " << r.exception().what();
                        dispatcher_->dispatchErr(r.exception());
                        return Poll<Item>(r.exception());
                    }
                } else {
                    transport_->shutdownWrite();
                    write_closed_ = true;
                    FUTURES_DLOG(INFO) << "write side closed";
                }
            } else {
                break;
            }
        }
        // flush
        auto r = sink_.pollComplete();
        if (r.hasException()) {
            FUTURES_DLOG(ERROR) << "write error: " << r.exception().what();
            dispatcher_->dispatchErr(r.exception());
            return Poll<Item>(r.exception());
        }

        if (!dispatcher_->has_in_flight() && read_closed_) {
            transport_->shutdownWrite();
            write_closed_ = true;
        }

        if (read_closed_ && write_closed_) {
            FUTURES_DLOG(INFO) << "rpc channel closed";
            dispatcher_->dispatchErr(IOError("Channel closed"));
            return makePollReady(folly::Unit());
        }

        return Poll<Item>(not_ready);
    }

    RpcFuture(
            io::Channel::Ptr transport,
            ReadStream&& stream, WriteSink&& sink,
            std::shared_ptr<DispatchType> dispatcher
            )
        :
        transport_(transport),
        stream_(std::move(stream)),
        sink_(std::move(sink)),
        dispatcher_(dispatcher) {
        }

private:
    io::Channel::Ptr transport_;
    ReadStream stream_;
    WriteSink sink_;
    std::shared_ptr<DispatchType> dispatcher_;
    // State s_ = INIT;
    bool read_closed_ = false;
    bool write_closed_ = false;
};

template <typename ReadStream, typename WriteSink, typename Service>
RpcFuture<ReadStream, WriteSink>
makePipelineRpcFuture(
        io::Channel::Ptr transport,
        ReadStream&& stream, WriteSink &&sink,
        std::shared_ptr<Service> service,
        size_t max_inflight = 1) {
    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    return RpcFuture<ReadStream, WriteSink>(transport,
            std::forward<ReadStream>(stream), std::forward<WriteSink>(sink),
            std::make_shared<PipelineDispatcher<Req, Resp>>(service, max_inflight));
}

template <typename ReadStream, typename WriteSink, typename Service>
RpcFuture<ReadStream, WriteSink>
makeMultiplexRpcFuture(
        io::Channel::Ptr transport,
        ReadStream&& stream, WriteSink &&sink,
        std::shared_ptr<Service> service,
        size_t max_inflight = 1024) {
    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    return RpcFuture<ReadStream, WriteSink>(transport,
            std::forward<ReadStream>(stream), std::forward<WriteSink>(sink),
            std::make_shared<MultiplexDispatcher<Req, Resp>>(service, max_inflight));
}

template <typename ReadStream, typename WriteSink, typename Dispatch>
RpcFuture<ReadStream, WriteSink>
makeRpcClientFuture(io::Channel::Ptr transport,
        ReadStream&& stream, WriteSink &&sink,
        std::shared_ptr<Dispatch> dispatch) {
    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    return RpcFuture<ReadStream, WriteSink>(
            transport,
            std::forward<ReadStream>(stream), std::forward<WriteSink>(sink),
            dispatch);
}


}
}

#pragma once

#include <deque>
#include <futures/io/IoFuture.h>
#include <futures/io/IoStream.h>
#include <futures/Promise.h>
#include <futures/Service.h>
#include <futures/Exception.h>

namespace futures {

template <typename Req, typename Resp = Req>
class IDispatcher {
public:
    virtual void dispatch(Req&& in) = 0;
    virtual void dispatchErr(folly::exception_wrapper err) = 0;
    virtual bool has_in_flight() = 0;
    virtual Poll<Optional<Resp>> poll() = 0;
    virtual ~IDispatcher() = default;
};

template <typename Req, typename Resp = Req>
class PipelineDispatcher : public IDispatcher<Req, Resp> {
public:
    PipelineDispatcher(std::shared_ptr<Service<Req, Resp>> service,
            size_t max_inflight = 1)
        : service_(service), max_inflight_(max_inflight) {}

    void dispatch(Req&& in) override {
        if (in_flight_.size() >= max_inflight_)
            throw DispatchException("too many inflight requests");
        auto f = (*service_)(std::move(in));
        in_flight_.push_back(std::move(f));
    }

    void dispatchErr(folly::exception_wrapper err) {}

    bool has_in_flight() {
        return !in_flight_.empty();
    }

    Poll<Optional<Resp>> poll() {
        if (in_flight_.empty())
            return Poll<Optional<Resp>>(not_ready);
        auto r = in_flight_.front().poll();
        if (r.hasException()) {
            in_flight_.pop_front();
            return Poll<Optional<Resp>>(r.exception());
        } else if (r->isReady()) {
            in_flight_.pop_front();
            return makePollReady(Optional<Resp>(folly::moveFromTry(r)));
        } else {
            return Poll<Optional<Resp>>(not_ready);
        }
    }

private:
    const size_t max_inflight_;
    std::shared_ptr<Service<Req, Resp>> service_;
    std::deque<BoxedFuture<Resp>> in_flight_;
};

template <typename Req, typename Resp = Req>
class PipelineClientDispatcher :
    public Service<Req, Resp>,
    public IDispatcher<Resp, Req> {
public:

    BoxedFuture<Resp> operator()(Req req) override {
        Promise<Resp> p;
        in_flight_.push_back(std::move(req));
        auto f = p.getFuture();
        promise_.push_back(std::move(p));
        notify();
        return f.boxed();
    }

    void dispatch(Resp&& in) override {
        // if (closed_)
        //     throw DispatchException("already closed");
        if (promise_.empty()) {
            throw DispatchException("unexpected server response");
        }
        promise_.front().setValue(std::move(in));
        promise_.pop_front();
    }

    void dispatchErr(folly::exception_wrapper err) override {
        for (auto &e: promise_)
            e.setException(err);
        closeNow();
    }

    Poll<Optional<Req>> poll() override {
        if (in_flight_.empty()) {
            if (closed_) {
                return makePollReady(Optional<Req>());
            } else {
                park();
                return Poll<Optional<Req>>(not_ready);
            }
        }
        auto req = makePollReady(Optional<Req>(std::move(in_flight_.front())));
        in_flight_.pop_front();
        return req;
    }

    bool has_in_flight() override {
        return !in_flight_.empty();
    }

    BoxedFuture<folly::Unit> close() override {
        closeNow();
        return makeOk().boxed();
    }

private:
    bool closed_ = false;
    std::deque<Req> in_flight_;
    std::deque<Promise<Resp>> promise_;
    Optional<Task> task_;

    void park() {
        task_ = CurrentTask::park();
    }

    void notify() {
        if (task_) task_->unpark();
        task_.clear();
    }

    void closeNow() {
        promise_.clear();
        in_flight_.clear();
        closed_ = true;
        notify();
    }
};

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
makeRpcFuture(
        io::Channel::Ptr transport,
        ReadStream&& stream, WriteSink &&sink,
        std::shared_ptr<Service> service) {
    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    return RpcFuture<ReadStream, WriteSink>(transport,
            std::forward<ReadStream>(stream), std::forward<WriteSink>(sink),
            std::make_shared<PipelineDispatcher<Req, Resp>>(service));
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

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
    virtual Poll<Resp> poll() = 0;
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

    void cancel() {
        in_flight_.clear();
    }

    bool has_in_flight() {
        return !in_flight_.empty();
    }

    Poll<Resp> poll() {
        if (in_flight_.empty())
            return Poll<Resp>(not_ready);
        auto r = in_flight_.front().poll();
        if (r.hasException() || r.value().isReady())
            in_flight_.pop_front();
        return std::move(r);
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
        FUTURES_CHECK (!promise_.empty());
        promise_.front().setValue(std::move(in));
        promise_.pop_front();
    }

    void dispatchErr(folly::exception_wrapper err) {
        for (auto &e: promise_)
            e.setException(err);
        promise_.clear();
        in_flight_.clear();
    }

    Poll<Req> poll() override {
        if (in_flight_.empty()) {
            park();
            return Poll<Req>(not_ready);
        }
        Poll<Req> req(makePollReady(std::move(in_flight_.front())));
        in_flight_.pop_front();
        return req;
    }

    bool has_in_flight() {
        return true;
    }

private:
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
                auto r = sink_.startSend(v.value());
                if (r.hasException()) {
                    FUTURES_DLOG(ERROR) << "encode frame error: " << r.exception().what();
                    dispatcher_->dispatchErr(r.exception());
                    return Poll<Item>(r.exception());
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

        if (!dispatcher_->has_in_flight() && read_closed_)
            write_closed_ = true;

        if (read_closed_ && write_closed_) {
            FUTURES_DLOG(INFO) << "rpc channel closed";
            return makePollReady(folly::Unit());
        }

        return Poll<Item>(not_ready);
    }

    RpcFuture(ReadStream &&stream,
            std::shared_ptr<DispatchType> service,
            WriteSink &&sink
            )
        : stream_(std::move(stream)),
        sink_(std::move(sink)),
        dispatcher_(service) {
        }

private:
    ReadStream stream_;
    WriteSink sink_;
    std::shared_ptr<DispatchType> dispatcher_;
    // State s_ = INIT;
    bool read_closed_ = false;
    bool write_closed_ = false;
};

template <typename ReadStream, typename Service, typename WriteSink>
RpcFuture<ReadStream, WriteSink>
makeRpcFuture(ReadStream&& stream, std::shared_ptr<Service> service, WriteSink &&sink) {
    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    return RpcFuture<ReadStream, WriteSink>(std::forward<ReadStream>(stream),
            std::make_shared<PipelineDispatcher<Req, Resp>>(service),
            std::forward<WriteSink>(sink));
}

}

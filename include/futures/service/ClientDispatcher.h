#pragma once

#include <deque>
#include <map>
#include <futures/service/Dispatch.h>
#include <futures/service/Service.h>
#include <futures/Promise.h>

namespace futures {
namespace service {

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

template <typename Req, typename Resp = Req>
class MultiplexClientDispatcher :
    public Service<Req, Resp>,
    public IDispatcher<Resp, Req> {
public:

    BoxedFuture<Resp> operator()(Req req) override {
        Promise<Resp> p;
        auto callid = req.getCallId();
        in_flight_.push_back(std::move(req));
        auto f = p.getFuture();
        promise_.insert(std::make_pair(callid, std::move(p)));
        notify();
        return f.boxed();
    }

    void dispatch(Resp&& in) override {
        // if (closed_)
        //     throw DispatchException("already closed");
        auto callid = in.getCallId();
        auto it = promise_.find(callid);
        if (it == promise_.end())
            throw DispatchException("unexpected server response with callid: " + std::to_string(callid));
        it->second.setValue(std::move(in));
        promise_.erase(it);
    }

    void dispatchErr(folly::exception_wrapper err) override {
        for (auto &e: promise_)
            e.second.setException(err);
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
    std::map<int64_t, Promise<Resp>> promise_;
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



}
}

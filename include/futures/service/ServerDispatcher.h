#pragma once

#include <deque>
#include <map>
#include <futures/service/Dispatch.h>
#include <futures/service/Service.h>
#include <futures/Promise.h>

namespace futures {
namespace service {

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

    void dispatchErr(folly::exception_wrapper err) override {}

    bool has_in_flight() override {
        return !in_flight_.empty();
    }

    Poll<Optional<Resp>> poll() override {
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
class MultiplexDispatcher : public IDispatcher<Req, Resp> {
public:
    MultiplexDispatcher(std::shared_ptr<Service<Req, Resp>> service,
            size_t max_inflight = 1024)
        : service_(service), max_inflight_(max_inflight) {}

    void dispatch(Req&& in) override {
        if (in_flight_.size() >= max_inflight_)
            throw DispatchException("too many inflight requests");
        int64_t callid = in.getCallId();
        in_flight_.insert(std::make_pair(callid, (*service_)(std::move(in))));
    }

    void dispatchErr(folly::exception_wrapper err) override {}

    bool has_in_flight() override {
        return !in_flight_.empty();
    }

    Poll<Optional<Resp>> poll() override {
        if (in_flight_.empty())
            return Poll<Optional<Resp>>(not_ready);
        for (auto it = in_flight_.begin(); it != in_flight_.end(); ++it) {
            auto r = it->second.poll();
            if (r.hasException()) {
                in_flight_.erase(it);
                return Poll<Optional<Resp>>(r.exception());
            } else if (r->isReady()) {
                in_flight_.erase(it);
                return makePollReady(Optional<Resp>(folly::moveFromTry(r)));
            }
        }
        return Poll<Optional<Resp>>(not_ready);
    }

private:
    const size_t max_inflight_;
    std::shared_ptr<Service<Req, Resp>> service_;
    std::map<int64_t, BoxedFuture<Resp>> in_flight_;
};


}
}

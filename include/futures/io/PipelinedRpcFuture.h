#pragma once

#include <futures/io/IoFuture.h>
#include <futures/Service.h>

namespace futures {

template <typename CODEC>
class PipelinedRpcFuture : public FutureBase<PipelinedRpcFuture<CODEC>, folly::Unit> {
public:
    using Item = folly::Unit;
    using Req = typename CODEC::In;
    using Resp = typename CODEC::Out;

    enum State {
        INIT,
        POLLING_MSG,
        POLLING_SERVICE,
        SENDING_MSG,
        CANCELLED,
        DONE,
    };

    PipelinedRpcFuture(std::shared_ptr<Service<Req, Resp>> service, io::FramedStream<CODEC>&& source, io::FramedSink<CODEC> &&sink)
        : service_(service), source_(std::move(source)), sink_(std::move(sink)) {
    }

    Poll<Item> poll() override {
        while (true) {
            switch (s_) {
            case INIT:
                s_ = POLLING_MSG;
                // fall through
            case POLLING_MSG: {
                auto r = source_.poll();
                if (r.hasException())
                    return Poll<Item>(r.exception());
                auto v = folly::moveFromTry(r);
                if (!v.isReady())
                    return Poll<Item>(not_ready);
                if (!v->hasValue()) {
                    s_ = DONE;
                    return makePollReady(folly::Unit());
                } else {
                    s_ = POLLING_SERVICE;
                    fut_.emplace((*service_)(std::move(v).value().value()));
                }
                break;
            }
            case POLLING_SERVICE: {
                auto r = fut_->poll();
                if (r.hasException()) {
                    fut_.clear();
                    s_ = DONE;
                    return Poll<Item>(r.exception());
                }
                if (r->isReady()) {
                    auto v = folly::moveFromTry(r);
                    result_ = std::move(v).value();
                    fut_.clear();
                    s_ = SENDING_MSG;
                } else {
                    return Poll<Item>(not_ready);
                }
                break;
            }
            case SENDING_MSG: {
                FUTURES_DLOG(INFO) << "start send";
                auto r = sink_.startSend(std::move(result_).value());
                if (r.hasException()) {
                    s_ = DONE;
                    return Poll<Item>(r.exception());
                }
                if (r->hasValue()) {
                    result_ = folly::moveFromTry(r);
                } else {
                    s_ = INIT;
                }
                break;
            }
            case CANCELLED:
                return Poll<Item>(FutureCancelledException());
            default:
                throw InvalidPollStateException();
            }
        }
    }

private:
    State s_ = INIT;
    std::shared_ptr<Service<Req, Resp>> service_;
    io::FramedStream<CODEC> source_;
    io::FramedSink<CODEC> sink_;

    Optional<BoxedFuture<Resp>> fut_;
    Optional<Resp> result_;
};

}

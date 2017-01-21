#pragma once

#include <deque>
#include <futures/io/IoFuture.h>
#include <futures/io/IoStream.h>
#include <futures/Service.h>

namespace futures {

template <typename Req, typename Resp = Req>
class PipelineDispatcher {
public:
    PipelineDispatcher(Service<Req, Resp>* service)
        : service_(service) {}

    void dispatch(Req&& in) {
        auto f = (*service_)(std::move(in));
        in_flight_.push_back(std::move(f));
    }

    void cancel() {
        for (auto &e: in_flight_)
            e.cancel();
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
    Service<Req, Resp>* service_;
    std::deque<BoxedFuture<Resp>> in_flight_;
};

template <typename ReadStream, typename WriteSink>
class Pipelined1Future : public FutureBase<Pipelined1Future<ReadStream, WriteSink>, folly::Unit> {
public:
    using Item = folly::Unit;

    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    using ServiceType = Service<Req, Resp>;

    Poll<Item> poll() override {
        FUTURES_DLOG(INFO) << "Pipeline::tick";
        // 1) process inbound frames
        while (!read_closed_) {
            FUTURES_DLOG(INFO) << "reading frames";
            auto r = stream_.poll();
            if (r.hasException())
                return Poll<Item>(r.exception());
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                if (v->hasValue()) {
                    dispatcher_.dispatch(std::move(v).value().value());
                } else {
                    // read-side closed
                    read_closed_ = true;
                    FUTURES_DLOG(INFO) << "Closed";
                }
            } else {
                break;
            }
        }

        // 2) process outbound frame
        while (!write_closed_) {
            auto r = dispatcher_.poll();
            if (r.hasException())
                return Poll<Item>(r.exception());
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                sink_.startSend(v.value());
            } else {
                break;
            }
        }
        // flush
        auto r = sink_.pollComplete();
        if (r.hasException())
            return Poll<Item>(r.exception());

        if (!dispatcher_.has_in_flight() && read_closed_)
            write_closed_ = true;

        if (read_closed_ && write_closed_)
            return makePollReady(folly::Unit());

        return Poll<Item>(not_ready);
    }

    Pipelined1Future(ReadStream &&stream,
            std::shared_ptr<ServiceType> service,
            WriteSink &&sink
            )
        : stream_(std::move(stream)),
        service_(service),
        sink_(std::move(sink)),
        dispatcher_(service_.get()) {
        }

    void cancel() override {
        dispatcher_.cancel();
    }

private:
    ReadStream stream_;
    std::shared_ptr<ServiceType> service_;
    WriteSink sink_;
    PipelineDispatcher<Req, Resp> dispatcher_;
    // State s_ = INIT;
    bool read_closed_ = false;
    bool write_closed_ = false;
};

template <typename ReadStream, typename Service, typename WriteSink>
Pipelined1Future<ReadStream, WriteSink>
makePipelineFuture(ReadStream&& stream, std::shared_ptr<Service> service, WriteSink &&sink) {
    return Pipelined1Future<ReadStream, WriteSink>(std::forward<ReadStream>(stream),
            service,
            std::forward<WriteSink>(sink));
}

template <typename Pipeline, typename ReadStream, typename WriteSink>
class PipelinedFuture : public FutureBase<PipelinedFuture<Pipeline, ReadStream, WriteSink>, folly::Unit> {
public:
    using Item = folly::Unit;
    using pipeline_ptr = typename Pipeline::Ptr;

    Poll<Item> poll() override {
        while (true) {
            auto r = stream_.poll();
            if (r.hasException())
                return Poll<Item>(r.exception());
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                if (v->hasValue()) {
                    pipeline_->read(*(v->value()));
                } else {
                    pipeline_->readEOF();
                    return makePollReady(folly::Unit());
                }
            } else {
                return Poll<Item>(not_ready);
            }
        }
    }

    PipelinedFuture(ReadStream &&stream,
            pipeline_ptr pipeline)
        : stream_(std::move(stream)),
          pipeline_(pipeline) {
    }

private:
    ReadStream stream_;
    pipeline_ptr pipeline_;
    // State s_ = INIT;
};

}

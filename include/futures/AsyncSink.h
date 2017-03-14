#pragma once

#include <futures/Async.h>
#include <futures/Future.h>

namespace futures {

template <typename T>
class IAsyncSink {
public:
    virtual Try<void> startSend(T&& item) = 0;
    virtual Poll<folly::Unit> pollComplete() = 0;
    virtual ~IAsyncSink() = default;
};

template <typename T>
class FlushSinkFuture;

template <typename Derived, typename T>
class AsyncSinkBase : public IAsyncSink<T> {
public:
    using Out = T;

    Try<void> startSend(T&& item) override {
        assert(0 && "cannot call base startSend");
        return Try<void>();
    }

    Poll<folly::Unit> pollComplete() override {
        assert(0 && "cannot call base pollComplete");
    }

    FlushSinkFuture<T> flush();
};

template <typename T>
class FlushSinkFuture : public FutureBase<FlushSinkFuture<T>, Unit> {
public:
    using Item = Unit;
    FlushSinkFuture(IAsyncSink<T> *sink)
        : sink_(sink) {}

    Poll<Item> poll() override {
        if (!sink_) throw FutureCancelledException();
        return sink_->pollComplete();
    }
private:
    IAsyncSink<T> *sink_;
};

template <typename Derived, typename T>
FlushSinkFuture<T> AsyncSinkBase<Derived, T>::flush() {
    return FlushSinkFuture<T>(this);
}

#if 0
template <typename Sink>
class SendSink : public FutureBase<SendSink<Sink>, Sink> {
public:
    using Item = Sink;

};
#endif

}

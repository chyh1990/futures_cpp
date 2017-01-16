#pragma once

#include <futures/Async.h>
#include <futures/Future.h>

namespace futures {

template <typename T>
using StartSend = Try<Optional<T>>;

template <typename T>
class IAsyncSink {
public:
    virtual StartSend<T> startSend(T&& item) = 0;
    virtual Poll<folly::Unit> pollComplete() = 0;
    virtual ~IAsyncSink() = default;
};


template <typename Derived, typename T>
class AsyncSinkBase : public IAsyncSink<T> {
public:
    using Out = T;

    StartSend<T> startSend(T&& item) override {
        assert(0 && "cannot call base startSend");
    }

    Poll<folly::Unit> pollComplete() override {
        assert(0 && "cannot call base pollComplete");
    }
};

#if 0
template <typename Sink>
class SendSink : public FutureBase<SendSink<Sink>, Sink> {
public:
    using Item = Sink;

};
#endif

}

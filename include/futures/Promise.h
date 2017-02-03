#pragma once
#include <futures/Future.h>
#include <futures/channel/OneShotChannel.h>

namespace futures {

class PromiseException : public std::runtime_error {
public:
    PromiseException()
        : std::runtime_error("invalid promise state") {}
};

template <typename T>
class PromiseFuture : public FutureBase<PromiseFuture<T>, T> {
public:
    Poll<T> poll() override {
        auto p = recv_.poll();
        if (p.hasException())
            return Poll<T>(p.exception());
        auto r = folly::moveFromTry(p);
        if (r.isReady()) {
            return makePollReady(std::move(r).value());
        } else {
            return Poll<T>(not_ready);
        }
    }

    PromiseFuture(channel::OneshotChannelReceiver<Try<T>> &&recv)
        : recv_(std::move(recv)) {
    }

private:
    channel::OneshotChannelReceiver<Try<T>> recv_;
};



template <typename T>
class Promise {
public:
    Promise() {
        auto p = channel::makeOneshotChannel<Try<T>>();
        s_ = std::move(p.first);
        r_ = std::move(p.second);
    }

    PromiseFuture<T> getFuture() {
        if (!r_.isValid()) throw PromiseException();
        return PromiseFuture<T>(std::move(r_));
    }

    void cancel() {
        s_.cancel();
    }

    template <typename V>
    void setValue(V&& v) {
        s_.send(Try<T>(std::forward<V>(v)));
    }

    void setException(folly::exception_wrapper ex) {
        s_.send(Try<T>(ex));
    }

private:
    channel::OneshotChannelSender<Try<T>> s_;
    channel::OneshotChannelReceiver<Try<T>> r_;
};


}

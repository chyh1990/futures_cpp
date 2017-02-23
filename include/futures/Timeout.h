#pragma once

#include <futures/Timer.h>
#include <futures/TimerKeeper.h>

namespace futures {

class TimeoutException: public std::runtime_error {
public:
    TimeoutException()
        : std::runtime_error("Timeout") {}
};

template <typename Fut, typename TimerFut>
class TimeoutFuture : public FutureBase<TimeoutFuture<Fut, TimerFut>, typename isFuture<Fut>::Inner>
{
public:
    typedef typename isFuture<Fut>::Inner Item;

    TimeoutFuture(Fut&& f, TimerFut&& timer)
        : f_(std::move(f)), timer_(std::move(timer))
    {
    }

    Poll<Item> poll() {
        auto ra = timer_->poll();
        if (ra.hasException()) {
            clear();
            return Poll<Item>(ra.exception());
        }
        auto va = folly::moveFromTry(ra);
        if (va.isReady()) {
            clear();
            return Poll<Item>(TimeoutException());
        }
        auto rb = f_->poll();
        if (rb.hasException()) {
            clear();
            return Poll<Item>(rb.exception());
        }
        if (rb.value().isReady()) {
            timer_.clear();
        }
        return std::move(rb);
    }

private:
    Optional<Fut> f_;
    Optional<TimerFut> timer_;

    void clear() {
        f_.clear();
        timer_.clear();
    }
};

template <typename Fut>
TimeoutFuture<Fut, TimerFuture>
timeout(EventExecutor* ev, Fut &&f, double after) {
    return TimeoutFuture<Fut, TimerFuture>(std::move(f),
            TimerFuture(ev, after));
}

template <typename Fut>
TimeoutFuture<Fut, TimerKeeperFuture>
timeout(TimerKeeper::Ptr timer, Fut &&f) {
    return TimeoutFuture<Fut, TimerKeeperFuture>(std::move(f),
            timer->timeout());
}


}

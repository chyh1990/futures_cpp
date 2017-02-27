#pragma once

#include <futures/Timer.h>
#include <futures/TimerKeeper.h>

namespace futures {

class TimeoutException: public std::runtime_error {
public:
    TimeoutException()
        : std::runtime_error("Timeout") {}

    TimeoutException(const std::string &source)
        : std::runtime_error("Timeout from " + source) {}
};

template <typename Fut, typename TimerFut>
class TimeoutFuture : public FutureBase<TimeoutFuture<Fut, TimerFut>, typename isFuture<Fut>::Inner>
{
public:
    using Item = typename isFuture<Fut>::Inner;

    TimeoutFuture(Fut&& f, TimerFut&& timer,
            const std::string &desc)
        : f_(std::move(f)), timer_(std::move(timer)), desc_(desc)
    {
    }

    Poll<Item> poll() {
        auto ra = timer_->poll();
        if (ra.hasException()) {
            clear();
            return Poll<Item>(ra.exception());
        }
        if (ra->isReady()) {
            clear();
            return Poll<Item>(desc_.empty() ?
                    TimeoutException() : TimeoutException(std::move(desc_)));
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
    std::string desc_;

    void clear() {
        f_.clear();
        timer_.clear();
    }
};

template <typename Fut>
TimeoutFuture<Fut, TimerFuture>
timeout(EventExecutor* ev, Fut &&f, double after,
        const std::string &desc = std::string()) {
    return TimeoutFuture<Fut, TimerFuture>(std::move(f),
            TimerFuture(ev, after), desc);
}

template <typename Fut>
TimeoutFuture<Fut, TimerKeeperFuture>
timeout(TimerKeeper::Ptr timer, Fut &&f,
        const std::string &desc = std::string()) {
    return TimeoutFuture<Fut, TimerKeeperFuture>(std::move(f),
            timer->timeout(), desc);
}


}

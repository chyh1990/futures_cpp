#pragma once

#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>

namespace futures {

class TimerIOHandler : public EventWatcherBase {
private:
    ev::timer timer_;
    Task task_;
    EventExecutor *reactor_;

public:
    TimerIOHandler(EventExecutor* reactor, Task task, double ts)
        : timer_(reactor->getLoop()), task_(task), reactor_(reactor) {
        FUTURES_DLOG(INFO) << "TimerHandler new";
        timer_.set(this);
        reactor_->linkWatcher(this);
        timer_.start(ts);
    }

    bool hasTimeout() {
        return timer_.remaining() <= 0.0;
    }

    void operator()(ev::timer &io, int revents) {
        FUTURES_DLOG(INFO) << "TimerHandler call";
        task_.unpark();
    }

    void cleanup(int reason) override {
        task_.unpark();
    }

    ~TimerIOHandler() {
        FUTURES_DLOG(INFO) << "TimerHandler stop";
        reactor_->unlinkWatcher(this);
        timer_.stop();
    }
};

class TimerFuture : public FutureBase<TimerFuture, std::error_code>
{
public:
    enum State {
        INIT,
        WAITING,
        TIMEOUT,
        CANCELLED,
    };

    typedef std::error_code Item;

    TimerFuture(EventExecutor *ev, double after)
        : reactor_(ev), after_(after), s_(INIT) {}

    Poll<Item> poll();

private:
    EventExecutor *reactor_;
    double after_;
    State s_;
    std::unique_ptr<TimerIOHandler> handler_;
};

class TimeoutException: public std::runtime_error {
public:
    TimeoutException()
        : std::runtime_error("Timeout") {}
};

template <typename Fut>
class TimeoutFuture : public FutureBase<TimeoutFuture<Fut>, typename isFuture<Fut>::Inner>
{
public:
    typedef typename isFuture<Fut>::Inner Item;

    TimeoutFuture(EventExecutor *ev, Fut f, double after)
        : f_(std::move(f)), timer_(TimerFuture(ev, after))
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
    Optional<TimerFuture> timer_;

    void clear() {
        f_.clear();
        timer_.clear();
    }
};

template <typename Fut>
TimeoutFuture<Fut> timeout(EventExecutor* ev, Fut &&f, double after) {
    return TimeoutFuture<Fut>(ev, std::forward<Fut>(f), after);
}

inline TimerFuture delay(EventExecutor* ev, double after) {
    return TimerFuture(ev, after);
}



}

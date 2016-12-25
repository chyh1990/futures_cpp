#pragma once

#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>

namespace futures {

class TimerIOHandler {
private:
    ev::timer timer_;
    Task task_;
    EventExecutor &reactor_;

public:
    TimerIOHandler(EventExecutor& reactor, Task task, double ts)
        : timer_(reactor.getLoop()), task_(task), reactor_(reactor) {
        std::cerr << "TimerHandlerHERE: " << std::endl;
        timer_.set(this);
        reactor_.incPending();
        timer_.start(ts);
    }

    bool hasTimeout() {
        return timer_.remaining() <= 0.0;
    }

    void operator()(ev::timer &io, int revents) {
        std::cerr << "TimerIOHandler() " << revents << std::endl;
        task_.unpark();
    }

    ~TimerIOHandler() {
        std::cerr << "TimerHandlerStop: " << std::endl;
        reactor_.decPending();
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

    TimerFuture(EventExecutor &ev, double after)
        : reactor_(ev), after_(after), s_(INIT) {}

    Poll<Item> poll();

    void cancel();

private:
    EventExecutor &reactor_;
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

    TimeoutFuture(EventExecutor &ev, Fut f, double after)
        : f_(std::move(f)), timer_(TimerFuture(ev, after))
    {
    }

    Poll<Item> poll() {
        auto ra = timer_.poll();
        if (ra.hasException()) {
            f_.cancel();
            return Poll<Item>(ra.exception());
        }
        auto va = folly::moveFromTry(ra);
        if (va.isReady()) {
            f_.cancel();
            return Poll<Item>(TimeoutException());
        }
        auto rb = f_.poll();
        if (rb.hasException())
            return Poll<Item>(rb.exception());
        if (rb.value().isReady())
            timer_.cancel();
        return std::move(rb);
    }

private:
    Fut f_;
    TimerFuture timer_;
};

template <typename Fut>
TimeoutFuture<Fut> timeout(EventExecutor& ev, Fut &&f, double after) {
    return TimeoutFuture<Fut>(ev, std::forward<Fut>(f), after);
}

}

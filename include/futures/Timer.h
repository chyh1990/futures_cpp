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

    void cleanup(CancelReason reason) override {
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

inline TimerFuture delay(EventExecutor* ev, double after) {
    return TimerFuture(ev, after);
}



}

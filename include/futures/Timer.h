#pragma once

#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>

namespace futures {

class TimerIOHandler {
private:
    ev::timer timer_;
    Task task_;

public:
    TimerIOHandler(EventExecutor& reactor, Task task, double ts)
        : timer_(reactor.getLoop()), task_(task) {
        std::cerr << "TimerHandlerHERE: " << std::endl;
        timer_.set(this);
        timer_.start(ts);
    }

    void operator()(ev::timer &io, int revents) {
        std::cerr << "TimerIOHandler() " << revents << std::endl;
        task_.unpark();
        delete this;
    }

private:
    ~TimerIOHandler() {
        std::cerr << "TimerHandlerStop: " << std::endl;
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
    };

    typedef std::error_code Item;

    TimerFuture(EventExecutor &ev, double after)
        : reactor_(ev), after_(after), s_(INIT) {}

    Poll<Item> poll();

private:
    EventExecutor &reactor_;
    double after_;
    State s_;
};

};

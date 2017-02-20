#pragma once

#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>
#include <futures/io/WaitHandleBase.h>

namespace futures {

class Timer: public io::IOObject {
public:
    enum State {
        INIT,
        WAITING,
        DONE,
        CANCELLED
    };

    Timer(EventExecutor* reactor, double ts)
        : io::IOObject(reactor),
        timer_(reactor->getLoop()) {
        FUTURES_DLOG(INFO) << "TimerHandler new";
        timer_.set<Timer, &Timer::onEvent>(this);
        timer_.set(ts);
    }

    void start() {
        if (s_ == INIT || s_ == CANCELLED) {
            timer_.start();
            s_ = WAITING;
            getExecutor()->linkWatcher(this);
        } else {
            throw InvalidPollStateException();
        }
    }

    bool hasTimeout() {
        return timer_.remaining() <= 0.0;
    }

    void onCancel(CancelReason reason) override {
        if (s_ == WAITING) {
            timer_.stop();
            getExecutor()->unlinkWatcher(this);
        }
        notify();
    }

    void park() {
        task_ = CurrentTask::park();
    }

    ~Timer() {
        onCancel(CancelReason::IOObjectShutdown);
    }

    State getState() const {
        return s_;
    }

private:
    ev::timer timer_;
    Optional<Task> task_;
    State s_ = INIT;

    void onEvent(ev::timer &io, int revents) {
        FUTURES_DLOG(INFO) << "TimerHandler call";
        if (revents & ev::TIMER) {
            getExecutor()->unlinkWatcher(this);
            s_ = DONE;
        }
        notify();
    }

    void notify() {
        if (task_) task_->unpark();
        task_.clear();
    }

};

class TimerFuture : public FutureBase<TimerFuture, folly::Unit>
{
public:
    TimerFuture(EventExecutor *ev, double after)
        : reactor_(ev), after_(after)  {}

    Poll<Item> poll() override;

private:
    EventExecutor *reactor_;
    double after_;
    std::unique_ptr<Timer> timer_;
};

inline TimerFuture delay(EventExecutor* ev, double after) {
    return TimerFuture(ev, after);
}



}

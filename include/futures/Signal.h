#pragma once

#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>

namespace futures {

class SignalIOHandler : public EventWatcherBase {
private:
    ev::sig sig_;
    Task task_;
    EventExecutor *reactor_;
    int signum_;
    bool signaled_ = false;

public:
    SignalIOHandler(EventExecutor *reactor, Task task, int signum)
        : sig_(reactor->getLoop()), task_(task),
        reactor_(reactor), signum_(signum) {
        FUTURES_DLOG(INFO) << "SignalHandler start";
        sig_.set(this);
        reactor_->linkWatcher(this);
        sig_.start(signum);
    }

    void operator()(ev::sig &io, int revents) {
        signaled_ = true;
        task_.unpark();
    }

    void cleanup(int reason) override {
        task_.unpark();
    }

    bool hasSignal() const { return signaled_; }

    ~SignalIOHandler() {
        FUTURES_DLOG(INFO) << "SignalHandler stop";
        reactor_->unlinkWatcher(this);
        sig_.stop();
    }
};

class SignalFuture : public FutureBase<SignalFuture, int>
{
public:
    typedef int Item;
    enum State {
        INIT,
        WAITING,
        DONE,
        CANCELLED,
    };

    SignalFuture(EventExecutor *ev, int signum)
        : ev_(ev), signum_(signum) {
    }

    Poll<Item> poll() override {
        switch (s_) {
        case INIT:
            handler_.reset(new SignalIOHandler(ev_,
                        *CurrentTask::current_task(), signum_));
            s_ = WAITING;
            // fall through
        case WAITING:
            if (handler_->hasSignal()) {
                handler_.reset();
                s_ = DONE;
                return makePollReady(signum_);
            }
            break;
        default:
            throw InvalidPollStateException();
        }
        return Poll<Item>(not_ready);
    }

    void cancel() override {
        handler_.reset();
        s_ = CANCELLED;
    }
private:
    State s_ = INIT;;
    EventExecutor* ev_;
    int signum_;
    std::unique_ptr<SignalIOHandler> handler_;
};

inline SignalFuture signal(EventExecutor* ev, int signum) {
    return SignalFuture(ev, signum);
}

}

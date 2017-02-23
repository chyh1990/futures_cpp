#pragma once

#include <deque>
#include <futures/Future.h>
#include <futures/io/WaitHandleBase.h>

namespace futures {

class TimerKeeperFuture;

class TimerKeeper :
    public io::IOObject,
    public std::enable_shared_from_this<TimerKeeper>
    {
public:
    using Ptr = std::shared_ptr<TimerKeeper>;

    TimerKeeper(EventExecutor *ev, double timeout)
        : io::IOObject(ev), timeout_(timeout), timer_(ev->getLoop()) {
        assert(timeout > 0);
        timer_.set<TimerKeeper, &TimerKeeper::onTimer>(this);
    }

    void onCancel(CancelReason reason) override {
        timer_.stop();
    }

    struct CompletionToken : public io::CompletionToken {
    public:
        CompletionToken(double deadline)
            : io::CompletionToken(IOObject::OpRead), deadline_(deadline) {}

        void onCancel(CancelReason r) override {
            // do nothing
            static_cast<TimerKeeper*>(getIOObject())->stopTimer(this);
        }

        double getDeadline() const { return deadline_; }

        void setDeadline(double deadline) {
            FUTURES_CHECK(getState() != STARTED);
            deadline_ = deadline;
        }

        void stop() {
            cleanup(CancelReason::UserCancel);
        }

        Poll<folly::Unit> poll() {
            switch (getState()) {
            case STARTED:
                park();
                return Poll<folly::Unit>(not_ready);
            case DONE:
                return makePollReady(folly::Unit());
            case CANCELLED:
                return Poll<folly::Unit>(FutureCancelledException());
            }
        }

    protected:
        ~CompletionToken() {
            stop();
        }

        double deadline_;
    };

    io::intrusive_ptr<CompletionToken> doTimeout() {
        io::intrusive_ptr<CompletionToken> p(new CompletionToken(getExecutor()->getNow() + timeout_));
        addTimer(p.get());
        return p;
    }

    io::intrusive_ptr<CompletionToken> doTimeout(std::unique_ptr<CompletionToken> tok) {
        io::intrusive_ptr<CompletionToken> p(tok.release());
        addTimer(p.get());
        return p;
    }


    inline TimerKeeperFuture timeout();

private:
    const double timeout_;
    ev::timer timer_;

    void addTimer(CompletionToken *tok) {
        // assert(!getPending().empty());
        tok->attach(this);
        auto p = &getPending(IOObject::OpRead).front();
        if (p != tok)
            return;
        timer_.stop();
        timer_.start(tok->getDeadline() - getExecutor()->getNow());
    }

    void stopTimer(CompletionToken *tok) {
        // do nothing
    }

    void onTimer(ev::timer &watcher, int rev) {
        if (rev & ev::ERROR)
            throw std::runtime_error("syscall error");
        if (rev & ev::TIMER) {
            auto &list = getPending(IOObject::OpRead);
            double now = getExecutor()->getNow();
            while (!list.empty()) {
                auto p = static_cast<CompletionToken*>(&list.front());
                if (now >= p->getDeadline()) {
                    p->notifyDone();
                } else {
                    break;
                }
            }
            if (list.empty()) {
                timer_.stop();
            } else {
                auto p = static_cast<CompletionToken*>(&list.front());
                assert(p->getDeadline() >= now);
                timer_.start(p->getDeadline() - now);
            }
        }
    }
};

class TimerKeeperFuture : public FutureBase<TimerKeeperFuture, folly::Unit> {
public:
    using Item = folly::Unit;

    explicit TimerKeeperFuture(TimerKeeper::Ptr ptr)
        : ctx_(ptr) {
    }

    Poll<Item> poll() {
        if (!tok_)
            tok_ = ctx_->doTimeout();
        return tok_->poll();
    }

private:
    TimerKeeper::Ptr ctx_;
    io::intrusive_ptr<TimerKeeper::CompletionToken> tok_;
};

TimerKeeperFuture TimerKeeper::timeout() {
    return TimerKeeperFuture(shared_from_this());
}

}

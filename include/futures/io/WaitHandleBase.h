#pragma once

#include <futures/EventExecutor.h>
#include <atomic>

namespace futures {
namespace io {

class CompletionToken;

class IOObject : private EventWatcherBase {
public:
    inline void attachChild(CompletionToken *tok);
    inline void dettachChild(CompletionToken *tok);

    void cleanup(int reason) override {
        while (!pending_.empty())
            pending_.front().cleanup(reason);
        onCancel();
    }

    IOObject(EventExecutor* ev) : ev_(ev) {}
    virtual ~IOObject() = default;

    EventExecutor *getExecutor() { return ev_; }

    virtual void onCancel() {}
private:
    EventExecutor *ev_;
    EventWatcherBase::EventList pending_;
};

class CompletionToken : private EventWatcherBase {
public:
    CompletionToken(IOObject *parent)
        : parent_(parent) {
        parent_->attachChild(this);
    }

    virtual void onCancel() = 0;

    void cleanup(int reason) override {
        if (s_ != STARTED) return;
        onCancel();
        parent_->dettachChild(this);
        s_ = CANCELLED;
        notify();
    }

    void notifyDone() {
        assert(s_ == STARTED);
        s_ = DONE;
        parent_->dettachChild(this);
        notify();
    }

    Try<bool> pollState() {
        switch (s_) {
            case STARTED:
                task_ = CurrentTask::park();
                return Try<bool>(false);
            case DONE:
                return Try<bool>(true);
            case CANCELLED:
                return Try<bool>(FutureCancelledException());
        };
    }

    virtual ~CompletionToken() {
        assert(!task_);
        assert(s_ != STARTED);
    }

    IOObject *getIOObject() {
        return parent_;
    }
private:
    IOObject *parent_;
    Optional<Task> task_;

    enum State {
        STARTED,
        DONE,
        CANCELLED,
    };
    State s_ = STARTED;

    void notify() {
        if (task_) task_->unpark();
        task_.clear();
    }

    friend class IOObject;
};

void IOObject::attachChild(CompletionToken *tok) {
    if (pending_.empty())
        ev_->linkWatcher(this);
    pending_.push_back(*tok);
}

void IOObject::dettachChild(CompletionToken *tok) {
    pending_.erase(EventWatcherBase::EventList::s_iterator_to(*tok));
    if (pending_.empty())
        ev_->unlinkWatcher(this);
}

}
}

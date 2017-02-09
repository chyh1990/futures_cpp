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

    IOObject *getIOObject() {
        return parent_;
    }

    void addRef() {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void decRef() {
        if (ref_count_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
        }
    }

protected:
    virtual ~CompletionToken() {
        assert(!task_);
        assert(s_ != STARTED);
    }

private:
    IOObject *parent_;
    Optional<Task> task_;
    std::atomic_size_t ref_count_{1};

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

template <typename T>
class intrusive_ptr {
public:
    intrusive_ptr()
        : ptr_(nullptr) {}

    intrusive_ptr(T* ptr)
        : ptr_(ptr) {
    }

    ~intrusive_ptr() {
        reset();
    }

    void reset() {
        if (ptr_) ptr_->decRef();
        ptr_ = nullptr;
    }

    intrusive_ptr& operator=(const intrusive_ptr& o) {
        if (this == &o) return *this;
        reset();
        ptr_ = o.ptr_;
        if (ptr_) ptr_->addRef();
        return *this;
    }

    intrusive_ptr(const intrusive_ptr& o)
        : ptr_(o.ptr_) {
        if (ptr_) ptr_->addRef();
    }

    intrusive_ptr(intrusive_ptr&& o)
        : ptr_(o.ptr_) {
        o.ptr_ = nullptr;
    }

    intrusive_ptr& operator=(intrusive_ptr&& o) {
        if (this == &o) return *this;
        reset();
        ptr_ = o.ptr_;
        o.ptr_ = nullptr;
        return *this;
    }

    T* operator->() {
        return ptr_;
    }

    const T* operator->() const {
        return ptr_;
    }

    T* get() { return ptr_; }
    const T* get() const { return ptr_; }

private:
    T *ptr_;
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

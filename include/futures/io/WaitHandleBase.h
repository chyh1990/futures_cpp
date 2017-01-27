#pragma once

#include <futures/EventExecutor.h>
#include <atomic>

namespace futures {
namespace io {

template <typename T>
class wait_handle_ptr;

template <typename T>
struct WaitHandleBase : public EventWatcherBase {
public:
    WaitHandleBase() {}

    void addRef() {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
        }
    }

    void unpark() {
        if (task_) task_->unpark();
        task_.clear();
    }

    virtual void cancel() {}

    virtual void cleanup(int r) override {
        cancel();
        unpark();
    }

    Try<T> &result() {
        return result_;
    }

    void set(Try<T> &&v) {
        result_ = std::move(v);
    }

    void set(const Try<T> &v) {
        result_ = v;
    }

    bool isReady() const {
        return result_.hasException() || result_.hasValue();
    }
protected:

    virtual ~WaitHandleBase() {
        FUTURES_DLOG(INFO) << "WaitHandle destory";
    }

    void park() {
        task_ = CurrentTask::park();
    }

    void clearTask() {
        task_.clear();
    }

    template <typename V>
    friend class wait_handle_ptr;

    Optional<Task> task_;
    Try<T> result_;
    std::atomic_size_t ref_count_{1};
};

template<typename T>
class wait_handle_ptr
{
public:
    wait_handle_ptr(T *ptr = nullptr)
        : pointer(ptr), is_owner(false) {}
    /*
    wait_handle_ptr<T>& operator=(T *ptr) {
        pointer = ptr;
        return *this;
    }
    */
    wait_handle_ptr(wait_handle_ptr<T> && other)
    {
        pointer = other.pointer;
        is_owner = other.is_owner;
        other.pointer = nullptr;
    }
    wait_handle_ptr<T>& operator=(wait_handle_ptr<T> && other)
    {
        pointer = other.pointer;
        is_owner = other.is_owner;
        other.pointer = nullptr;
        return *this;
    }
    ~wait_handle_ptr() {
        reset();
    }

    T* operator->() const { return pointer; }
    T& operator*() const { return *pointer; }

    void reset() {
        if (pointer) {
            if (is_owner) {
                pointer->cancel();
                pointer->clearTask();
            }
            pointer->release();
        }
        pointer = nullptr;
        is_owner = false;
    }

    T* get() { return pointer; }
    const T* get() const { return pointer; }

    wait_handle_ptr(wait_handle_ptr<T> const & other)
        : pointer(other.pointer), is_owner(other.is_owner()) {
        if (pointer) pointer->addRef();
    }

    wait_handle_ptr<T> & operator=(wait_handle_ptr<T> const & other) {
        if (this == &other) return;
        reset();
        pointer = other.pointer;
        is_owner = other.is_owner;
        if (pointer) pointer->addRef();
        return *this;
    }

    bool operator==(T *ptr) { return pointer == ptr; }
    bool operator!=(T *ptr) { return pointer != ptr; }
    operator bool() { return pointer != nullptr; }

    void park() {
        pointer->park();
        is_owner = true;
    }
private:
    T *pointer;
    bool is_owner;
};

}
}

#pragma once

#include <cassert>
#include <atomic>
#include <mutex>
#include <memory>
#include <iostream>
#include <condition_variable>
#include <futures/detail/ThreadLocalData.h>

namespace futures {

namespace detail {

inline unsigned long newTaskId() {
    static std::atomic_ulong id(0);
    return ++id;
}

}

class Unpark {
public:
    virtual ~Unpark() {}
    virtual void unpark() = 0;
};

class ThreadUnpark : public Unpark {
public:
    virtual void unpark() {
        std::unique_lock<std::mutex> g(mu_);
        ready_ = true;
        cv_.notify_all();
        FUTURES_DLOG(INFO) << "Unpark";
    }

    void park() {
        std::unique_lock<std::mutex> g(mu_);
        if (ready_) return;
        ready_ = false;
        FUTURES_DLOG(INFO) << "PARKING";
        while (!ready_)
            cv_.wait(g);
    }

    ThreadUnpark(): ready_(false) {}
private:
    bool ready_;
    std::mutex mu_;
    std::condition_variable cv_;
};

class Task {
public:
    unsigned long Id() const { return id_; }

    Task(unsigned long id, std::shared_ptr<Unpark> unpark):
        id_(id),
        unpark_(unpark),
        data_(nullptr) {
        assert(unpark_);
    }

    void unpark() {
        unpark_->unpark();
    }
private:
    unsigned long id_;
    std::shared_ptr<Unpark> unpark_;
    void *data_;
};

class CurrentTask : public ThreadLocalData<CurrentTask, Task> {
public:
    using WithGuard = ThreadLocalData<CurrentTask, Task>::WithGuard;

    static Task *current_task() { return current(); }
};


}

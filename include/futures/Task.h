#pragma once

#include <cassert>
#include <atomic>
#include <mutex>
#include <iostream>
#include <condition_variable>

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
        std::cerr << "Unpark" << std::endl;
    }

    void park() {
        std::unique_lock<std::mutex> g(mu_);
        if (ready_) return;
        ready_ = false;
        std::cerr << "PARKING" << std::endl;
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

    Task(unsigned long id, Unpark *unpark):
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
    Unpark *unpark_;
    void *data_;
};

class CurrentTask {
public:
    class WithGuard {
    public:
        WithGuard(CurrentTask *c, Task* t)
            : c_(c), old_(c->current_) {
            c->current_ = t;
        }

        ~WithGuard() { c_->current_ = old_; }
    private:
        CurrentTask *c_;
        Task *old_;
    };

    static CurrentTask* this_thread() {
        thread_local CurrentTask _tls;
        return &_tls;
    }

    static Task *current_task() {
        return this_thread()->current_;
    }

    // template <typename F>
    // void set(Task *task, F&& f) {
    //     WithGuard g(this, task);
    //     F();
    // }

private:
    CurrentTask() : current_(nullptr) {}

    Task *current_;
};


}

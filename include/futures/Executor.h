#pragma once

#include <atomic>
#include <memory>
#include <boost/intrusive/list.hpp>
#include <futures/detail/ThreadLocalData.h>

namespace futures {

class Runnable : public boost::intrusive::list_base_hook<> {
public:
    enum Type {
        NORMAL = 0,
        SHUTDOWN,
    };

    virtual void run() = 0;
    virtual ~Runnable() = default;

    Type type() const { return type_; }
protected:
    Runnable(): type_(NORMAL) {}
    Runnable(Type t): type_(t) {}

private:
    Type type_;
};

class ShutdownRunnable : public Runnable {
public:
    ShutdownRunnable()
        : Runnable(Runnable::SHUTDOWN) {}
    void run() {}
};

class Executor {
public:
    virtual void execute(std::unique_ptr<Runnable> run) = 0;
    virtual void stop() = 0;

    void addRunning() { running_tasks_++; }
    void decRunning() { running_tasks_--; }
    size_t getRunning() { return running_tasks_; }

    virtual ~Executor() = default;
    Executor() {}
private:
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    std::atomic_size_t running_tasks_{0};
};

class CurrentExecutor: public ThreadLocalData<CurrentExecutor, Executor> {
public:
    using WithGuard = ThreadLocalData<CurrentExecutor, Executor>::WithGuard;
};

}

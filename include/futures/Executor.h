#pragma once

#include <memory>
#include <boost/intrusive/list.hpp>

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
    virtual ~Executor() = default;
    Executor() {}
private:
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
};

}

#pragma once

namespace futures {

class Runnable {
public:
    virtual void run() = 0;
    virtual ~Runnable() = default;
};

class Executor {
public:
    virtual void execute(Runnable *run) = 0;
    virtual ~Executor() = default;
    Executor() {}
private:
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
};

}

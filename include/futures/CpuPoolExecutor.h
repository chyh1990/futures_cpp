#pragma once

#include <thread>

#include <mutex>
#include <condition_variable>
#include <queue>
#include <futures/Executor.h>
#include <futures/Future.h>
#include <futures/Channel.h>

namespace futures {

class CpuPoolExecutor;

template <typename T>
class CpuFutureImpl: public FutureImpl<T> {
public:
    typedef typename FutureImpl<T>::poll_type poll_type;

    poll_type poll() override {
        return poll_type(recv_.poll());
    }

private:
    channel::OnshotChannel<T> recv_;

    CpuFutureImpl(channel::OnshotChannel<T> recv)
        : recv_(recv) {
    }

    friend CpuPoolExecutor;

};

template <typename T, typename F>
class CpuRunnable : public Runnable {
public:
    // sender side
    CpuRunnable(channel::OnshotChannel<T> sender,
            F&& fn)
        : sender_(sender), fn_(std::move(fn)) {
    }

    virtual void run() override {
        std::cerr << "HERE" << std::endl;
        sender_.send(fn_());
        // unpark
    }

    virtual ~CpuRunnable() {}
private:
    channel::OnshotChannel<T> sender_;
    F fn_;
};

class CpuPoolExecutor : public Executor {
public:
    CpuPoolExecutor(size_t num_threads) {
        for (size_t i = 0; i < num_threads; ++i) {
            pool_.push_back(std::thread([this] {
                worker();
            }));
        }
    }

    virtual ~CpuPoolExecutor() {
        shutdown();
    }

    virtual void execute(Runnable *run) override {
        std::unique_lock<std::mutex> g(mu_);
        q_.push(run);
        cv_.notify_one();
    }

    template <typename F,
             typename R = typename std::result_of<F()>::type>
    Future<R> spawn(F&& f) {
        channel::OnshotChannel<R> channel;
        Runnable *runnable = new CpuRunnable<R, F>(channel, std::move(f));
        execute(runnable);
        return Future<R>(new CpuFutureImpl<R>(channel));
    }

private:
    std::vector<std::thread> pool_;
    std::queue<Runnable*> q_;
    std::mutex mu_;
    std::condition_variable cv_;

    void shutdown() {
        for (size_t i = 0; i < pool_.size(); ++i)
            q_.push(nullptr);
        cv_.notify_all();
        for (auto &e: pool_)
            e.join();
    }

    void worker() {
        while (true) {
            std::unique_lock<std::mutex> g(mu_);
            while (q_.empty())
                cv_.wait(g);
            Runnable *run = q_.front();
            q_.pop();
            g.unlock();
            if (!run) break;

            run->run();
            delete run;
        }
    }
};

}


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
    Poll<T> poll() override {
        auto r = recv_.poll();
        if (r.isReady()) {
            Try<T> v = std::move(r).value();
            if (v.hasException())
                return Poll<T>(v.exception());
            else
                return Poll<T>(Async<T>(std::move(v).value()));
        } else {
            return Poll<T>(Async<T>());
        }
    }

    CpuFutureImpl(channel::OnshotChannel<Try<T>> recv)
        : recv_(recv) {
    }

private:
    channel::OnshotChannel<Try<T>> recv_;

    friend CpuPoolExecutor;
};

template <typename T>
class CpuFutureSenderImpl : public FutureImpl<folly::Unit> {
public:
    CpuFutureSenderImpl(Future<T> fut, channel::OnshotChannel<Try<T>> sender)
        : fut_(std::move(fut)), sender_(std::move(sender)) {
    }

    Poll<folly::Unit> poll() {
        auto r = fut_.poll();
        if (r.hasException()) {
            sender_.send(Try<T>(r.exception()));
        } else {
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                sender_.send(Try<T>(std::move(v).value()));
            } else {
                return Poll<folly::Unit>(Async<folly::Unit>());
            }
        }
        return Poll<folly::Unit>(Async<folly::Unit>(folly::Unit()));
    }

private:
    Future<T> fut_;
    channel::OnshotChannel<Try<T>> sender_;
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

    virtual void execute(std::unique_ptr<Runnable> run) override {
        std::unique_lock<std::mutex> g(mu_);
        q_.push(run.release());
        cv_.notify_one();
    }

    template <typename T>
    Future<T> spawn(Future<T> fut) {
        channel::OnshotChannel<Try<T>> channel;
        Future<folly::Unit> sender(folly::make_unique<CpuFutureSenderImpl<T>>(std::move(fut), channel));

        execute(folly::make_unique<FutureSpawnRun>(this, FutureSpawn<folly::Unit>(std::move(sender))));
        return Future<T>(folly::make_unique<CpuFutureImpl<T>>(channel));
    }

    template <typename F,
             typename R = typename std::result_of<F()>::type>
    Future<R> spawn_fn(F&& f) {
        return spawn<R>(Future<R>::lazy(std::forward<F>(f)));
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


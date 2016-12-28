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
class CpuReceiveFuture: public FutureBase<CpuReceiveFuture<T>, T> {
public:
    Poll<T> poll() override {
        auto p = recv_.poll();
        if (p.hasException())
            return Poll<T>(p.exception());
        auto r = folly::moveFromTry(p);
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

    CpuReceiveFuture(channel::OneshotChannelReceiver<Try<T>> recv)
        : recv_(std::move(recv)) {
    }

private:
    channel::OneshotChannelReceiver<Try<T>> recv_;
};

template <typename Fut>
class CpuSenderFuture : public FutureBase<CpuSenderFuture<Fut>, folly::Unit> {
public:
    typedef folly::Unit Item;
    typedef typename isFuture<Fut>::Inner Data;
    CpuSenderFuture(Fut fut, channel::OneshotChannelSender<Try<Data>> sender)
        : fut_(std::move(fut)), sender_(std::move(sender)) {
    }

    Poll<folly::Unit> poll() {
        auto r = fut_.poll();
        if (r.hasException()) {
            sender_.send(Try<Data>(r.exception()));
        } else {
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                sender_.send(Try<Data>(std::move(v).value()));
            } else {
                return Poll<folly::Unit>(Async<folly::Unit>());
            }
        }
        return Poll<folly::Unit>(Async<folly::Unit>(folly::Unit()));
    }

private:
    Fut fut_;
    channel::OneshotChannelSender<Try<Data>> sender_;
};

class CpuPoolExecutor : public Executor {
public:
    CpuPoolExecutor(size_t num_threads)
        : is_running_(true) {
        for (size_t i = 0; i < num_threads; ++i) {
            pool_.push_back(std::thread([this] {
                worker();
            }));
        }
    }

    virtual ~CpuPoolExecutor() {
        stop();
    }

    void execute(std::unique_ptr<Runnable> run) override {
        std::unique_lock<std::mutex> g(mu_);
        // XXX dropping the run is enough?
        if (!is_running_) return;
        q_.push_back(*run.release());
        cv_.notify_one();
    }

    void stop() override {
        shutdown();
    }

    template <typename Fut, typename R = typename isFuture<Fut>::Inner>
    CpuReceiveFuture<R> spawn(Fut fut) {
        // channel::OnshotChannel<Try<R>> channel;
        auto ch = channel::makeOneshotChannel<Try<R>>();
        CpuSenderFuture<Fut> sender(std::move(fut), std::move(ch.first));

        execute(folly::make_unique<FutureSpawnRun>(this,
                    FutureSpawn<BoxedFuture<folly::Unit>>(sender.boxed())));
        return CpuReceiveFuture<R>(std::move(ch.second));
    }

    template <typename F,
             typename R = typename std::result_of<F()>::type>
    CpuReceiveFuture<R> spawn_fn(F&& f) {
        return spawn(LazyFuture<R, F>(std::forward<F>(f)));
    }

private:
    std::vector<std::thread> pool_;
    // std::queue<Runnable*> q_;
    boost::intrusive::list<Runnable> q_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool is_running_;

    void shutdown() {
        std::unique_lock<std::mutex> g(mu_);
        if (!is_running_) return;
        is_running_ = false;
        for (size_t i = 0; i < pool_.size(); ++i) {
            Runnable *r = new ShutdownRunnable();
            q_.push_back(*r);
        }
        cv_.notify_all();
        g.unlock();

        for (auto &e: pool_)
            e.join();
    }

    void worker() {
        while (true) {
            std::unique_lock<std::mutex> g(mu_);
            while (q_.empty()) {
                cv_.wait(g);
            }
            Runnable *run = &q_.front();
            q_.pop_front();
            // Runnable *run = q_.front();
            // q_.pop();
            g.unlock();
            if (!run) break;
            if (run->type() == Runnable::SHUTDOWN)
                break;

            run->run();
            delete run;
        }
    }
};

}


#pragma once

#include <thread>
#include <random>
#include <futures/EventExecutor.h>

namespace futures {

class EventThreadPool {
public:
    EventThreadPool(size_t threads)
        : thread_count_(threads), gen_(rd_()) {
    }

    EventExecutor *getExecutor() {
        std::uniform_int_distribution<> dis(0, thread_count_ - 1);
        int id = dis(gen_);
        return executors_[id];
    }

    template <typename Fut>
    void spawn(Fut&& fut) {
        getExecutor()->spawn(std::forward<Fut>(fut));
    }

    void start() {
        FUTURES_CHECK(executors_.empty()) << "has started";
        for (size_t i = 0; i < thread_count_; ++i)
            executors_.push_back(new EventExecutor());
        for (size_t i = 0; i < thread_count_; ++i)
            threads_.push_back(std::thread([this, i] () {
                executors_[i]->run(true);
            }));
    }

    void stop() {
        for (size_t i = 0; i < thread_count_; ++i) {
            executors_[i]->spawn(makeLazy([] () {
                EventExecutor::current()->stop();
                return unit;
            }));
        }
    }

    void join() {
        for (size_t i = 0; i < thread_count_; ++i) {
            threads_[i].join();
            delete executors_[i];
        }
        threads_.clear();
        executors_.clear();
    }

    ~EventThreadPool() {
        if (!executors_.empty())
            join();
    }

private:
    size_t thread_count_;
    std::random_device rd_;
    std::mt19937 gen_;
    std::vector<EventExecutor*> executors_;
    std::vector<std::thread> threads_;
};

}

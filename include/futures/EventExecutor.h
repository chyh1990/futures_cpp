#pragma once

#include <mutex>
#include <futures/Executor.h>
#include <futures/EventLoop.h>
#include <futures/Future.h>

namespace futures {

class EventExecutor : public Executor {
public:
    EventExecutor() {}
    ~EventExecutor() {}

    void execute(std::unique_ptr<Runnable> run) {
        q_.push_back(*run.release());
    }

    template <typename Fut>
    void run(Fut fut) {
        execute(folly::make_unique<FutureSpawnRun>(this,
                    FutureSpawn<BoxedFuture<folly::Unit>>(fut.boxed())));
        while (true) {
            if (!q_.empty()) {
                std::cerr << "QSIZE: " << q_.size() << std::endl;
                Runnable *run = &q_.front();
                q_.pop_front();
                run->run();
                delete run;
            }
            if (!pending_)
                break;
            std::cerr << "START POLL" << std::endl;
            loop_.run(EVRUN_ONCE);
            std::cerr << "END POLL" << std::endl;
        }
    }

    ev::dynamic_loop &getLoop() { return loop_; }

    void incPending() { pending_++; }
    void decPending() {
        assert(pending_ > 0);
        pending_--;
    }
private:
    ev::dynamic_loop loop_;
    int64_t pending_ = 0;
    boost::intrusive::list<Runnable> q_;
};

}

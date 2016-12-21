#pragma once

#include <mutex>
#include <futures/Executor.h>
#include <futures/EventLoop.h>
#include <futures/Future.h>

namespace futures {

class EventExecutor : public Executor {
public:
    EventExecutor();
    ~EventExecutor();

    void execute(std::unique_ptr<Runnable> run);

    template <typename Fut>
    void run(Fut fut) {
        execute(folly::make_unique<FutureSpawnRun>(this,
                    FutureSpawn<BoxedFuture<folly::Unit>>(fut.boxed())));
        while (!q_.empty()) {
            std::cerr << "QSIZE: " << q_.size() << std::endl;
            Runnable *run = &q_.front();
            q_.pop_front();
            run->run();
            delete run;
            if (!pending_)
                break;
            std::cerr << "START POLL" << std::endl;
            loop_->pollOnce();
            std::cerr << "END POLL" << std::endl;
        }
    }

    EventLoop &getLoop() { return *loop_; }

    void incPending() { pending_++; }
    void decPending() {
        assert(pending_ > 0);
        pending_--;
    }
private:
    EventLoop *loop_;
    int64_t pending_;
    boost::intrusive::list<Runnable> q_;
};

}

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

    void execute(std::unique_ptr<Runnable> run) override {
        if (wait_stop_) return;
        q_.push_back(*run.release());
    }

    void stop() override {
        wait_stop_ = true;
    }

    template <typename Fut>
    void spawn(Fut&& fut) {
        execute(folly::make_unique<FutureSpawnRun>(this,
                    FutureSpawn<BoxedFuture<folly::Unit>>(fut.boxed())));
    }

    void run() {
        while (true) {
            while (!q_.empty()) {
                std::cerr << "QSIZE: " << q_.size() << std::endl;
                Runnable *run = &q_.front();
                q_.pop_front();
                run->run();
                delete run;
            }
            if (pendings_.empty())
                break;
            if (wait_stop_) break;
            std::cerr << "START POLL" << std::endl;
            loop_.run(EVRUN_ONCE);
            std::cerr << "END POLL" << std::endl;
        }
        // cleanup
        while (!pendings_.empty()) {
            assert(wait_stop_);
            EventWatcherBase &n = pendings_.front();
            n.cleanup(0);
            // no pop here, front node will should be removed by cleanup
            assert(&pendings_.front() != &n);
        }

        // we may still have some pe
        wait_stop_ = false;
    }

    ev::dynamic_loop &getLoop() { return loop_; }

    // void incPending() { pending_++; }
    // void decPending() {
    //     assert(pending_ > 0);
    //     pending_--;
    // }
    void linkWatcher(EventWatcherBase *watcher) {
        pendings_.push_back(*watcher);
    }

    void unlinkWatcher(EventWatcherBase *watcher) {
        pendings_.erase(EventWatcherBase::EventList::s_iterator_to(*watcher));
    }
private:
    ev::dynamic_loop loop_;
    // int64_t pending_ = 0;
    EventWatcherBase::EventList pendings_;
    boost::intrusive::list<Runnable> q_;
    std::atomic_bool wait_stop_{false};
};

}

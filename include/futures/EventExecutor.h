#pragma once

#include <mutex>
#include <futures/Executor.h>
#include <futures/EventLoop.h>
#include <futures/Future.h>

namespace futures {

class EventExecutor : public Executor {
public:
    EventExecutor(bool is_main = false)
        : dyn_loop_(is_main ? nullptr : new ev::dynamic_loop()),
          signaler_(getLoop()) {
        signaler_.set<EventExecutor, &EventExecutor::async_callback>(this);
    }
    ~EventExecutor() {}

    void execute(std::unique_ptr<Runnable> run) override {
        auto cur = CurrentExecutor::current();
        if (cur && cur != this) {
            FUTURES_DLOG(INFO) << "foreign execute: " << run.get();
            std::lock_guard<std::mutex> _g(mu_);
            if (wait_stop_) return;
            foreign_q_.push_back(*run.release());
            signal_loop();
        } else {
            q_.push_back(*run.release());
        }
    }

#if 0
    void execute_other(std::unique_ptr<Runnable> run) {
        std::lock_guard<std::mutex> _g(mu_);
        if (wait_stop_) return;
        foreign_q_.push_back(*run.release());
    }
#endif

    void stop() override {
        wait_stop_ = true;
        signal_loop();
    }

    void signal_loop() {
        if (!signaler_.async_pending())
            signaler_.send();
    }

    template <typename Fut>
    void spawn(Fut&& fut) {
        auto ptr = folly::make_unique<FutureSpawnRun>(this,
                    FutureSpawn<BoxedFuture<folly::Unit>>(fut.boxed()));
        execute(std::move(ptr));
    }

    static EventExecutor *current() {
        return static_cast<EventExecutor*>(CurrentExecutor::current());
    }

    void run(bool always_blocks = false) {
        CurrentExecutor::WithGuard ctx_guard(CurrentExecutor::this_thread(), this);
        FUTURES_DLOG(INFO) << "event loop start: " << this;
        signaler_.start();
        while (true) {
            merge_queue();
            while (!q_.empty()) {
                FUTURES_DLOG(INFO) << "QSIZE: " << q_.size();
                Runnable *run = &q_.front();
                q_.pop_front();
                run->run();
                delete run;
            }
            if (pendings_.empty()) {
                FUTURES_DLOG(INFO) << "no pending events";
                break;
            }
            if (wait_stop_) {
                // cleanup
                FUTURES_DLOG(INFO) << "cleaning up";
                while (!pendings_.empty()) {
                    EventWatcherBase &n = pendings_.front();
                    n.cleanup(CancelReason::ExecutorShutdown);
                    // no pop here, front node will should be removed by cleanup
                    assert(&pendings_.front() != &n);
                }
            } else {
                FUTURES_DLOG(INFO) << "START POLL: " << this;
                getLoop().run(EVRUN_ONCE);
                FUTURES_DLOG(INFO) << "END POLL: " << this;
            }
        }
        signaler_.stop();
        // we may still have some pe
        wait_stop_ = false;
        FUTURES_DLOG(INFO) << "event loop end: " << this;
    }

    ev::loop_ref getLoop() {
        return dyn_loop_ ? *dyn_loop_ : ev::get_default_loop();
    }

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

    double getNow() {
        return getLoop().now();
    }
private:
    std::unique_ptr<ev::dynamic_loop> dyn_loop_;
    // int64_t pending_ = 0;
    EventWatcherBase::EventList pendings_;
    boost::intrusive::list<Runnable> q_;
    boost::intrusive::list<Runnable> foreign_q_;
    std::atomic_bool wait_stop_{false};

    std::mutex mu_;
    ev::async signaler_;

    void merge_queue() {
        std::lock_guard<std::mutex> _g(mu_);
        while(!foreign_q_.empty()) {
            auto &f = foreign_q_.front();
            foreign_q_.pop_front();
            q_.push_back(f);
        }
    }

    void async_callback(ev::async& async, int revent) {
        // noop
    }
};

}

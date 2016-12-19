#pragma once

#include <atomic>
#include <memory>
#include <futures/core/Optional.h>
#include <futures/Task.h>
#include <futures/Async.h>

namespace futures {

namespace channel {

template <typename T>
class OnshotChannelImpl {
public:
    enum Status {
        NotReady = 0,
        Ready = 1,
    };

    OnshotChannelImpl()
        : s_(NotReady) {
    }

    void close() {
    }

    void send(T &&v) {
        std::lock_guard<std::mutex> g(mu_);
        assert(s_ == NotReady);
        v_ = std::move(v);
        s_ = Ready;
        if (rx_task_.hasValue())
            rx_task_->unpark();
    }

    Async<T> poll() {
        std::lock_guard<std::mutex> g(mu_);
        if (s_ == Ready)
            return Async<T>(std::move(std::move(v_).value()));

        rx_task_ = *CurrentTask::current_task();
        return Async<T>();
    }

private:
    std::mutex mu_;
    Status s_;  // use as memory barrier
    Optional<T> v_;
    Optional<Task> rx_task_;
};

template <typename T>
class OnshotChannel {
public:
    OnshotChannel()
        : impl_(std::make_shared<OnshotChannelImpl<T>>())
    {
    }

    void send(T &&t) {
        impl_->send(std::move(t));
    }

    Async<T> poll() {
        return impl_->poll();
    }

private:
    std::shared_ptr<OnshotChannelImpl<T>> impl_;
};

}

}

#pragma once

#include <atomic>
#include <memory>
#include <futures/core/Optional.h>
#include <futures/Exception.h>
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
        Closed = 2,
    };

    OnshotChannelImpl()
        : s_(NotReady) {
    }

    void send(T &&v) {
        std::lock_guard<std::mutex> g(mu_);
        if (s_ != NotReady) return;
        v_ = std::move(v);
        s_ = Ready;
        if (rx_task_.hasValue())
            rx_task_->unpark();
    }

    Poll<T> poll() {
        std::lock_guard<std::mutex> g(mu_);
        if (s_ == Closed)
            return Poll<T>(folly::make_exception_wrapper<FutureCancelledException>());
        if (s_ == Ready)
            return Poll<T>(Async<T>(std::move(std::move(v_).value())));

        rx_task_ = *CurrentTask::current_task();
        return Poll<T>(Async<T>());
    }

    void closeSender() {
        std::lock_guard<std::mutex> g(mu_);
        if (s_ == NotReady) {
            s_ = Closed;
            if (rx_task_.hasValue())
                rx_task_->unpark();
        }
    }

    void closeReceiver() {
        std::lock_guard<std::mutex> g(mu_);
        s_ = Closed;
    }

private:
    std::mutex mu_;
    Status s_;  // use as memory barrier
    Optional<T> v_;
    Optional<Task> rx_task_;
};

template <typename T>
class OneshotChannelSender {
public:
    OneshotChannelSender(std::shared_ptr<OnshotChannelImpl<T>> c)
        : impl_(c) {}

    ~OneshotChannelSender() {
        if (impl_)
            impl_->closeSender();
    }

    void send(T &&v) {
        assert(impl_.get() != nullptr);
        impl_->send(std::move(v));
    }

    OneshotChannelSender(const OneshotChannelSender&) = delete;
    OneshotChannelSender& operator=(const OneshotChannelSender&) = delete;
    OneshotChannelSender(OneshotChannelSender&&) = default;
    OneshotChannelSender& operator=(OneshotChannelSender&&) = default;
private:
    std::shared_ptr<OnshotChannelImpl<T>> impl_;
};

template <typename T>
class OneshotChannelReceiver {
public:
    OneshotChannelReceiver(std::shared_ptr<OnshotChannelImpl<T>> c)
        : impl_(c) {}

    ~OneshotChannelReceiver() {
        if (impl_)
            impl_->closeReceiver();
    }

    Poll<T> poll() {
        assert(impl_.get() != nullptr);
        return impl_->poll();
    }

    OneshotChannelReceiver(const OneshotChannelReceiver&) = delete;
    OneshotChannelReceiver& operator=(const OneshotChannelReceiver&) = delete;
    OneshotChannelReceiver(OneshotChannelReceiver&&) = default;
    OneshotChannelReceiver& operator=(OneshotChannelReceiver&&) = default;
private:
    std::shared_ptr<OnshotChannelImpl<T>> impl_;
};

template <typename T>
std::pair<OneshotChannelSender<T>, OneshotChannelReceiver<T>>
makeOneshotChannel() {
    auto p = std::make_shared<OnshotChannelImpl<T>>();
    return std::make_pair(OneshotChannelSender<T>(p),
            OneshotChannelReceiver<T>(p));
}

#if 0
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
#endif

}

}

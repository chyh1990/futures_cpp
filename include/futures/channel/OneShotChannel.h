#pragma once

#include <futures/channel/ChannelBase.h>

namespace futures {
namespace channel {

template <typename T, class Lock = std::mutex>
class OnshotChannelImpl {
public:
    using Item = T;
    enum Status {
        NotReady = 0,
        Ready = 1,
        Closed = 2,
    };

    OnshotChannelImpl()
        : s_(NotReady) {
    }

    template <typename V>
    bool send(V &&v) {
        std::lock_guard<Lock> g(mu_);
        if (s_ == Closed) return false;
        if (s_ != NotReady) throw InvalidChannelStateException();
        v_ = std::forward<V>(v);
        s_ = Ready;
        notify();
        return true;
    }

    Poll<T> poll() {
        std::lock_guard<Lock> g(mu_);
        if (s_ == Closed)
            return Poll<T>(FutureCancelledException());
        if (s_ == Ready) {
            if (v_)
                return Poll<T>(Async<T>(std::move(v_).value()));
            else
                return Poll<T>(FutureCancelledException());
        }

        rx_task_ = CurrentTask::park();
        return Poll<T>(not_ready);
    }

    void addSender() {}
    void addReceiver() {}

    void cancel() {
        std::lock_guard<Lock> g(mu_);
        if (s_ == Ready) throw InvalidChannelStateException();
        s_ = Ready;
        notify();
    }

    void closeSender() {
        std::lock_guard<Lock> g(mu_);
        if (s_ == NotReady) {
            s_ = Closed;
            notify();
        }
    }

    void closeReceiver() {
        std::lock_guard<std::mutex> g(mu_);
        s_ = Closed;
    }

private:
    Lock mu_;
    Status s_;  // use as memory barrier
    Optional<T> v_;
    Optional<Task> rx_task_;

    void notify() {
        if (rx_task_) rx_task_->unpark();
        rx_task_.clear();
    }
};

template <typename T, typename Lock = std::mutex>
using OneshotChannelSender = BasicSender<OnshotChannelImpl<T, Lock>>;
template <typename T, typename Lock = std::mutex>
using OneshotChannelReceiver = BasicReceiver<OnshotChannelImpl<T, Lock>>;

template <typename T, typename Lock = std::mutex>
std::pair<OneshotChannelSender<T, Lock>, OneshotChannelReceiver<T, Lock>>
makeOneshotChannel() {
    auto p = std::make_shared<OnshotChannelImpl<T, Lock>>();
    return std::make_pair(OneshotChannelSender<T, Lock>(p),
            OneshotChannelReceiver<T, Lock>(p));
}

}
}

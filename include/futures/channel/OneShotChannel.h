#pragma once

#include <futures/channel/ChannelBase.h>

namespace futures {
namespace channel {

template <typename T>
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
        std::lock_guard<std::mutex> g(mu_);
        if (s_ == Closed) return false;
        if (s_ != NotReady) throw InvalidChannelStateException();
        v_ = std::forward<V>(v);
        s_ = Ready;
        if (rx_task_.hasValue())
            rx_task_->unpark();
        return true;
    }

    Poll<T> poll() {
        std::lock_guard<std::mutex> g(mu_);
        if (s_ == Closed)
            return Poll<T>(FutureCancelledException());
        if (s_ == Ready)
            return Poll<T>(Async<T>(std::move(std::move(v_).value())));

        rx_task_ = CurrentTask::park();
        return Poll<T>(not_ready);
    }

    void addSender() {}
    void addReceiver() {}

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
using OneshotChannelSender = BasicSender<OnshotChannelImpl<T>>;
template <typename T>
using OneshotChannelReceiver = BasicReceiver<OnshotChannelImpl<T>>;

template <typename T>
std::pair<OneshotChannelSender<T>, OneshotChannelReceiver<T>>
makeOneshotChannel() {
    auto p = std::make_shared<OnshotChannelImpl<T>>();
    return std::make_pair(OneshotChannelSender<T>(p),
            OneshotChannelReceiver<T>(p));
}

}
}

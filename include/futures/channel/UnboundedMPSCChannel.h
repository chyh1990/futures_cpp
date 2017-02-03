#pragma once

#include <futures/channel/ChannelBase.h>
// #include <futures/channel/detail/mpsc-queue.hpp>
// #include <boost/lockfree/queue.hpp>
#include <queue>

namespace futures {
namespace channel {

template <typename T>
class UnboundedMPSCChannelImpl {
public:
    using Item = T;

    UnboundedMPSCChannelImpl() {
    }

    template <typename V>
    bool send(V &&v) {
        std::lock_guard<std::mutex> g(mu_);
        if (recv_closed_)
            return false;
        q_.push(std::forward<V>(v));
        if (rx_task_.hasValue()) {
            rx_task_->unpark();
        }
        return true;
    }

    Poll<T> poll() {
        std::lock_guard<std::mutex> g(mu_);
        if (recv_closed_) return Poll<T>(InvalidPollStateException());
        if (q_.empty()) {
            if (sender_closed_) {
                rx_task_.clear();
                return Poll<T>(FutureCancelledException());
            }
            rx_task_ = CurrentTask::park();
            return Poll<T>(not_ready);
        }
        T v = std::move(q_.front());
        q_.pop();

        return makePollReady(std::move(v));
    }

    void addSender() {
        senders_ ++;
    }
    void addReceiver() {}

    void closeSender() {
        auto t = senders_.fetch_sub(1);
        if (t <= 1) {
            std::lock_guard<std::mutex> g(mu_);
            sender_closed_ = true;
            if (rx_task_) rx_task_->unpark();
        }
    }

    void closeReceiver() {
        std::lock_guard<std::mutex> g(mu_);
        recv_closed_ = true;
        rx_task_.clear();
    }

    void cancel() { throw InvalidChannelStateException(); }

private:
    std::mutex mu_;
    std::queue<T> q_; // TODO: use lockfree impl
    Optional<Task> rx_task_;
    bool recv_closed_ = false;
    bool sender_closed_ = false;
    std::atomic_size_t senders_{0};
};

template <typename T>
using UnboundedMPSCChannelReceiver = BasicReceiver<UnboundedMPSCChannelImpl<T>>;

template <typename T>
class UnboundedMPSCChannelSender : public BasicSender<UnboundedMPSCChannelImpl<T>>
{
    using Item = T;

    using channel_type = UnboundedMPSCChannelImpl<T>;
    using base_type = BasicSender<channel_type>;
public:
    UnboundedMPSCChannelSender(std::shared_ptr<channel_type> c)
        : base_type(c) {}

    UnboundedMPSCChannelSender(const UnboundedMPSCChannelSender& o)
        : base_type(o.impl_) {
    }

    UnboundedMPSCChannelSender& operator=(const UnboundedMPSCChannelSender& o) {
        base_type::impl_ = o.impl_;
    }
};

template <typename T>
std::pair<UnboundedMPSCChannelSender<T>, UnboundedMPSCChannelReceiver<T>>
makeUnboundedMPSCChannel() {
    auto p = std::make_shared<UnboundedMPSCChannelImpl<T>>();
    return std::make_pair(UnboundedMPSCChannelSender<T>(p),
            UnboundedMPSCChannelReceiver<T>(p));
}

}
}


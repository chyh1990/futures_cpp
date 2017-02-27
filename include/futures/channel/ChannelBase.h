#pragma once

#include <atomic>
#include <memory>
#include <futures/core/Optional.h>
#include <futures/Exception.h>
#include <futures/Task.h>
#include <futures/Async.h>

namespace futures {
namespace channel {

struct NullLock {
    bool try_lock() { return true; }
    void lock() {}
    void unlock() {}
};

template <typename Channel>
class BasicSender {
public:
    using T = typename Channel::Item;

    BasicSender(std::shared_ptr<Channel> c)
        : impl_(c) {
        c->addSender();
    }

    BasicSender() {}

    virtual ~BasicSender() {
        if (impl_)
            impl_->closeSender();
    }

    bool send(T &&v) {
        assert(impl_.get() != nullptr);
        return impl_->send(std::move(v));
    }

    bool send(const T &v) {
        assert(impl_.get() != nullptr);
        return impl_->send(v);
    }

    void cancel() {
        return impl_->cancel();
    }

    bool isValid() const { return !! impl_; }

    BasicSender(const BasicSender&) = delete;
    BasicSender& operator=(const BasicSender&) = delete;
    BasicSender(BasicSender&&) = default;
    BasicSender& operator=(BasicSender&&) = default;
protected:
    std::shared_ptr<Channel> impl_;
};

template <typename Channel>
class BasicReceiver {
public:
    using T = typename Channel::Item;
    using Item = T;

    BasicReceiver(std::shared_ptr<Channel> c)
        : impl_(c) {}

    BasicReceiver() {}

    virtual ~BasicReceiver() {
        if (impl_)
            impl_->closeReceiver();
    }

    Poll<T> poll() {
        assert(impl_.get() != nullptr);
        return impl_->poll();
    }

    bool isValid() const { return !! impl_; }

    BasicReceiver(const BasicReceiver&) = delete;
    BasicReceiver& operator=(const BasicReceiver&) = delete;
    BasicReceiver(BasicReceiver&&) = default;
    BasicReceiver& operator=(BasicReceiver&&) = default;
protected:
    std::shared_ptr<Channel> impl_;
};


}
}

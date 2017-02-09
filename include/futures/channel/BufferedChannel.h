#pragma once

#include <deque>
#include <futures/Promise.h>

namespace futures {

template <typename T>
class BufferedChannel {
public:
  using Item = T;
  BufferedChannel(size_t max_size)
    : max_size_(max_size) {}

  template <typename V>
  PromiseFuture<folly::Unit> send(V&& v) {
    std::unique_lock<std::mutex> g(mu_);
    if (q_.size() < max_size_) {
      q_.push_back(std::move(v));
      notifyReader();
      return makeReadyPromiseFuture(folly::Unit());
    }
    Promise<folly::Unit> p;
    auto f = p.getFuture();
    tx_task_.push_back(std::make_pair(std::move(p), std::forward<V>(v)));
    return f;
  }

  template <typename V>
  bool trySend(V&& v) {
    std::unique_lock<std::mutex> g(mu_);
    if (q_.size() < max_size_) {
      q_.push_back(std::move(v));
      notifyReader();
      return true;
    }
    return false;
  }

  PromiseFuture<T> recv() {
    std::unique_lock<std::mutex> g(mu_);
    if (!q_.empty()) {
      auto f = makeReadyPromiseFuture(std::move(q_.front()));
      q_.pop_front();
      notifyWriter();
      return f;
    }
    Promise<T> p;
    auto f = p.getFuture();
    rx_task_.push_back(std::move(p));
    return f;
  }

  Optional<T> tryRecv() {
    std::unique_lock<std::mutex> g(mu_);
    if (!q_.empty()) {
        auto f = folly::make_optional(std::move(q_.front()));
        q_.pop_front();
        notifyWriter();
        return f;
    }
    return folly::none;
  }

  size_t size() const {
      std::lock_guard<std::mutex> g(mu_);
      return q_.size();
  }

private:
  mutable std::mutex mu_;
  const size_t max_size_;
  std::deque<T> q_;
  std::deque<Promise<T>> rx_task_;
  std::deque<std::pair<Promise<folly::Unit>, T>> tx_task_;

  void notifyReader() {
      while (!rx_task_.empty() && q_.size() > 0) {
          auto p = std::move(rx_task_.front());
          rx_task_.pop_front();
          // XXX need copy
          if (p.setValue(q_.front()))
              q_.pop_front();
      }
  }

  void notifyWriter() {
    while (!tx_task_.empty() && q_.size() < max_size_) {
      auto p = std::move(tx_task_.front());
      tx_task_.pop_front();
      q_.push_back(std::move(p.second));
      p.first.setValue(folly::Unit());
    }
  }
};

}

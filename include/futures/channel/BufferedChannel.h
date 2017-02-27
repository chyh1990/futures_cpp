#pragma once

#include <deque>
#include <futures/Promise.h>
#include <boost/intrusive/list.hpp>

namespace futures {
namespace channel {

template <typename T>
class RecvFuture;

template <typename T>
class SendFuture;

template <typename T>
class BufferedChannel
  : public std::enable_shared_from_this<BufferedChannel<T>> {
public:
  using Item = T;
  using Ptr = std::shared_ptr<BufferedChannel<T>>;

  class ReadWaiter : public boost::intrusive::list_base_hook<> {
  public:
    ReadWaiter(BufferedChannel *parent)
      : ptr_(parent) {
      ptr_->addReader(this);
      v_.assignLeft(CurrentTask::park());
    }

    ReadWaiter(BufferedChannel *parent, T&& v)
      : ptr_(parent), v_(folly::right_tag, std::move(v)) {
    }

    void setValue(T&& v) {
      assert(ptr_);
      auto task = std::move(v_).left();
      v_.assignRight(std::move(v));
      task.unpark();
    }

    ~ReadWaiter() {
      detach();
    }

    Optional<T> value() {
      std::unique_lock<std::mutex> g(ptr_->mu_);
      if (v_.hasRight())
        return Optional<T>(std::move(v_).right());
      return none;
    }
  private:
    BufferedChannel *ptr_;
    folly::Either<Task, T> v_;

    void detach() {
      std::unique_lock<std::mutex> g(ptr_->mu_);
      if (this->is_linked())
        ptr_->closeReader(this);
      assert(!this->is_linked());
      ptr_ = nullptr;
    }
  };

  class WriteWaiter : public boost::intrusive::list_base_hook<> {
  public:
    template <typename U>
    WriteWaiter(BufferedChannel *parent, U&& v)
      : ptr_(parent) {
      ptr_->addWriter(this);
      v_ = std::make_pair(CurrentTask::park(), std::forward<U>(v));
    }

    WriteWaiter(BufferedChannel *p)
      : ptr_(p) {
    }

    T moveValue() {
      auto v = std::move(v_->second);
      v_->first.unpark();
      v_.clear();
      ptr_ = nullptr;
      return v;
    }

    bool isReady() const {
      std::unique_lock<std::mutex> g(ptr_->mu_);
      return !v_.hasValue();
    }

    ~WriteWaiter() {
      detach();
    }
  private:
    BufferedChannel *ptr_;
    Optional<std::pair<Task, T>> v_;

    void detach() {
      std::unique_lock<std::mutex> g(ptr_->mu_);
      if (this->is_linked())
        ptr_->closeWriter(this);
      assert(!this->is_linked());
      ptr_ = nullptr;
    }
  };

  friend class ReadWaiter;
  friend class WriteWaiter;

  using read_waiter = ReadWaiter;
  using write_waiter = WriteWaiter;

  BufferedChannel(size_t max_size)
    : max_size_(max_size) {}

  template <typename V>
  std::unique_ptr<write_waiter> doSend(V&& v) {
    std::unique_lock<std::mutex> g(mu_);
    if (q_.size() < max_size_) {
      q_.push_back(std::move(v));
      notifyReader();
      return folly::make_unique<write_waiter>(this);
    }
    return folly::make_unique<write_waiter>(this, std::forward<V>(v));
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

  std::unique_ptr<read_waiter> doRecv() {
    std::unique_lock<std::mutex> g(mu_);
    if (!q_.empty()) {
      auto f = folly::make_unique<read_waiter>(this, std::move(q_.front()));
      q_.pop_front();
      notifyWriter();
      return f;
    }
    return folly::make_unique<read_waiter>(this);
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

  // Future API
  RecvFuture<T> recv();

  template <typename U>
  SendFuture<T> send(U &&v);

private:
  mutable std::mutex mu_;
  const size_t max_size_;
  std::deque<T> q_;
  using write_wait_list = boost::intrusive::list<write_waiter>;
  using read_wait_list = boost::intrusive::list<read_waiter>;
  write_wait_list writers_;
  read_wait_list readers_;

  void notifyReader() {
      while (!readers_.empty() && q_.size() > 0) {
        readers_.front().setValue(std::move(q_.front()));
        q_.pop_front();
        readers_.pop_front();
      }
  }

  void notifyWriter() {
    while (!writers_.empty() && q_.size() < max_size_) {
      q_.push_back(writers_.front().moveValue());
      writers_.pop_front();
    }
  }

  void addWriter(write_waiter *o) noexcept {
    writers_.push_back(*o);
  }

  void addReader(read_waiter *o) noexcept {
    readers_.push_back(*o);
  }

  void closeWriter(write_waiter *o) noexcept {
    writers_.erase(write_wait_list::s_iterator_to(*o));
  }

  void closeReader(read_waiter *o) noexcept {
    readers_.erase(read_wait_list::s_iterator_to(*o));
  }

};

template <typename T>
class RecvFuture : public FutureBase<RecvFuture<T>, T> {
  using waiter = typename BufferedChannel<T>::ReadWaiter;
public:
  using Item = T;

  RecvFuture(typename BufferedChannel<T>::Ptr ch)
    : ch_(ch)
  {}

  Poll<Item> poll() override {
    if (!w_)
      w_ = ch_->doRecv();
    auto r = w_->value();
    if (r)
      return makePollReady(std::move(r).value());
    return Poll<Item>(not_ready);
  }

private:
  typename BufferedChannel<T>::Ptr ch_;
  std::unique_ptr<waiter> w_;
};

template <typename T>
class SendFuture : public FutureBase<SendFuture<T>, Unit> {
  using waiter = typename BufferedChannel<T>::WriteWaiter;
public:
  using Item = Unit;

  template <typename U>
  SendFuture(typename BufferedChannel<T>::Ptr ch, U &&v)
    : ch_(ch) {
    w_ = ch_->doSend(std::forward<U>(v));
  }

  Poll<Item> poll() override {
    if (w_->isReady())
      return makePollReady(unit);
    return Poll<Item>(not_ready);
  }
private:
  typename BufferedChannel<T>::Ptr ch_;
  std::unique_ptr<waiter> w_;
};

template <typename T>
RecvFuture<T> BufferedChannel<T>::recv() {
  return RecvFuture<T>(this->shared_from_this());
}

template <typename T>
template <typename U>
SendFuture<T> BufferedChannel<T>::send(U &&v) {
  return SendFuture<T>(this->shared_from_this(), std::forward<U>(v));
}



}
}

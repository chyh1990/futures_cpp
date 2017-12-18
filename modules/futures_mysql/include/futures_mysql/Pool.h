#pragma once

#include <string>
#include <deque>
#include <memory>
#include <futures/Future.h>
#include <futures_mysql/Connection.h>

namespace futures {
namespace mysql {

class Pool : public io::IOObject {
public:
  using Ptr = std::shared_ptr<Pool>;

  template <typename... Args>
  static Ptr create(Args&&... args) {
    return std::make_shared<Pool>(std::forward<Args>(args)...);
  }

  Pool(EventExecutor *ev, const Config &c, size_t max_idle,
      double max_idle_time = 0)
    : io::IOObject(ev), config_(c),
      max_idles_(max_idle), max_idle_time_(max_idle_time),
      timer_(ev->getLoop())
  {
    timer_.set<Pool, &Pool::onTimer>(this);
    if (max_idle_time_ > 0) {
      double sleep = max_idle_time_ * 0.5;
      timer_.start(sleep, sleep);
    }
  }

  BoxedFuture<Connection::Ptr> getConnection() {
    if (conns_.size()) {
      auto p = conns_.back();
      conns_.pop_back();
      return makeOk(std::move(p)).boxed();
    } else {
      return Connection::connect(getExecutor(), config_).boxed();
    }
  }

  BoxedFuture<folly::Unit> checkin(Connection::Ptr conn) {
    if (conn->isIdle()) {
      // only reuse GOOD connections
      if (conns_.size() < max_idles_ && !conn->getErrors()) {
        conns_.push_back(conn);
        return makeOk().boxed();
      } else {
        return conn->close().boxed();
      }
    } else {
      FUTURES_LOG(ERROR) << "bad connection, dropping";
      return makeOk().boxed();
    }
  }

  void onCancel(CancelReason r) override {
    timer_.stop();
  }

  size_t getMaxIdles() const { return max_idles_; }
  size_t getIdleCount() const { return conns_.size(); }

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;
private:
  const Config config_;
  const size_t max_idles_;
  const double max_idle_time_;
  std::deque<Connection::Ptr> conns_;
  ev::timer timer_;

  void reapConnections() {
    if (conns_.empty()) return;
    if (max_idle_time_ <= 0) return;
    auto now = getExecutor()->getNow();
    size_t reaped = 0;
    for (auto it = conns_.begin(); it != conns_.end();) {
      if ((*it)->getLastUsedTimestamp() + max_idle_time_ <= now) {
        auto p = *it;
        it = conns_.erase(it);
        getExecutor()->spawn(p->close());
        reaped++;
      } else {
        ++it;
      }
    }
    if (reaped)
      FUTURES_DLOG(INFO) << "reaped connections: " << reaped;
  }

  void onTimer(ev::timer &timer, int revent) {
    if (revent & ev::TIMER)
      reapConnections();
  }
};


}
}

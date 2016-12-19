#pragma once

#include <memory>
#include <iostream>
#include <futures/core/Memory.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>

#include <futures/Async.h>
#include <futures/Executor.h>
#include <futures/Task.h>
#include <futures/UnparkMutex.h>
#include <futures/Future-pre.h>

namespace futures {

class InvalidPollStateException : public std::runtime_error {
 public:
  InvalidPollStateException()
      : std::runtime_error("Cannot poll twice") {}
};

class MovedFutureException: public std::runtime_error {
public:
  MovedFutureException()
      : std::runtime_error("Cannot use moved future") {}
};

template <typename T>
class FutureImpl {
public:
    typedef T item_type;

    virtual Poll<T> poll() = 0;
    virtual ~FutureImpl() = default;
};

template <typename T>
class EmptyFutureImpl : public FutureImpl<T> {
public:
    Poll<T> poll() override {
        return Poll<T>(Async<T>());
    }
};

template <typename T>
class ErrFutureImpl : public FutureImpl<T> {
public:
    Poll<T> poll() override {
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
        return Poll<T>(std::move(e_));
    }

    ErrFutureImpl(folly::exception_wrapper e)
        : consumed_(false), e_(std::move(e)) {
    }
private:
    bool consumed_;
    folly::exception_wrapper e_;
};

template <typename T>
class OkFutureImpl : public FutureImpl<T> {
public:
    Poll<T> poll() override {
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
        return Poll<T>(Async<T>(std::move(v_)));
    }

    OkFutureImpl(const T& v)
        : consumed_(false), v_(v) {
    }

    OkFutureImpl(T&& v) noexcept
        : consumed_(false), v_(std::move(v)) {
    }

private:
    bool consumed_;
    T v_;
};

template <typename T, typename F>
class LazyFutureImpl: public FutureImpl<T> {
public:
  LazyFutureImpl(F &&fn)
    : fn_(std::move(fn)) {}

    Poll<T> poll() override {
      try {
        return Poll<T>(Async<T>(fn_()));
      } catch (const std::exception &e) {
        return Poll<T>(folly::exception_wrapper(std::current_exception(), e));
      }
    }
private:
  F fn_;
};

template <typename T>
class Future {
public:
    typedef T item_type;

    Try<Async<item_type>> poll() {
        return impl_->poll();
    };

    static Future<T> empty() {
        return Future<T>(folly::make_unique<EmptyFutureImpl<T>>());
    }

    static Future<T> err(folly::exception_wrapper e) {
        return Future<T>(folly::make_unique<ErrFutureImpl<T>>(e));
    }

    // move & copy?
    static Future<T> ok(T&& v) {
        return Future<T>(folly::make_unique<OkFutureImpl<T>>(std::forward<T>(v)));
    }

    static Future<T> ok(const T& v) {
        return Future<T>(folly::make_unique<OkFutureImpl<T>>(v));
    }

    template <typename F>
    static Future<T> lazy(F&& f) {
        typedef typename std::result_of<F()>::type R;
        return Future<T>(folly::make_unique<LazyFutureImpl<R, F>>(std::forward<F>(f)));
    }

    template <typename F>
    typename detail::argResult<false, F, T>::Result
    andThen(F&& f);

    template <typename F>
    typename detail::argResult<false, F, Try<T>>::Result
    then(F&& f);

    ~Future() {
    }

    explicit Future(std::unique_ptr<FutureImpl<T>> impl)
      : impl_(std::move(impl)) {
      std::cerr << impl_.get() << std::endl;
    }

    Poll<T> wait();

    Async<T> value() {
      return wait().value();
    }

    Future(Future&&) = default;
    Future& operator=(Future&&) = default;

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
private:
    std::unique_ptr<FutureImpl<T>> impl_;

    Future<T> move_this() {
      return std::move(*this);
    }

    void validFuture() {
      if (!impl_) throw MovedFutureException();
    }
};

template <typename A, typename F>
class ChainStateMachine {
    enum class State {
        First,
        Second,
        Done,
    };
public:
    typedef typename detail::argResult<false, F, Try<A>>::Result f_result;
    static_assert(isFuture<f_result>::value, "chain callback must returns Future");

    typedef typename f_result::item_type b_type;
    typedef Try<Async<b_type>> poll_type;

    ChainStateMachine(Future<A> a, F&& fn)
        : state_(State::First), a_(std::move(a)), handler_(std::move(fn)) {
    }

    poll_type poll() {
        switch (state_) {
            case State::First: {
                auto p = a_.poll();
                if (p.hasValue()) {
                  auto v = folly::moveFromTry(p);
                  if (v.isReady()) {
                    return poll_second(Try<A>(std::move(v.value())));
                  } else {
                    return poll_type(Async<b_type>());
                  }
                } else {
                    return poll_second(Try<A>(p.exception()));
                }
                break;
            }
            case State::Second:
                return b_->poll();
            default:
                throw InvalidPollStateException();
        }
        // state_ = State::Second;
    }

private:
    poll_type poll_second(Try<A>&& a_result) {
        state_ = State::Second;
        // TODO skip
        try {
          b_ = handler_(std::move(a_result));
          return b_->poll();
        } catch (const std::exception &e) {
          return poll_type(folly::exception_wrapper(std::current_exception(), e));
        }
    }

    State state_;
    Future<A> a_;
    folly::Optional<f_result> b_;
    F handler_;
};

template <typename A, typename F>
ChainStateMachine<A, F> makeChain(Future<A> &&a, F&& f) {
    return ChainStateMachine<A, F>(std::move(a), std::forward<F>(f));
}

template <typename T, typename F, typename R>
class ThenFutureImpl: public FutureImpl<R> {
public:
  typedef typename ChainStateMachine<T, F>::poll_type poll_type;

  poll_type poll() override {
    return state_.poll();
  }

  ThenFutureImpl(Future<T> a, F&& f)
    : state_(std::move(a), std::move(f)) {
  }

private:
  ChainStateMachine<T, F> state_;
};

// fused Task & Future
template <typename T>
class FutureSpawn {
public:
    typedef Try<Async<T>> poll_type;

    poll_type poll_future(Unpark *unpark) {
        Task task(id_, unpark);

        CurrentTask::WithGuard g(CurrentTask::this_thread(), &task);
        return toplevel_.poll();
    }

    poll_type wait_future() {
        ThreadUnpark unpark;
        while (true) {
            auto r = poll_future(&unpark);
            if (r.hasException())
                return r;
            auto async = folly::moveFromTry(r);
            if (async.isReady()) {
                return poll_type(std::move(async));
            } else {
                unpark.park();
            }
        }
    }

    explicit FutureSpawn(Future<T> f)
      : id_(detail::newTaskId()), toplevel_(std::move(f)) {
    }

    FutureSpawn(FutureSpawn&&) = default;
    FutureSpawn& operator=(FutureSpawn&&) = default;
private:
    // toplevel future or stream
    unsigned long id_;
    Future<T> toplevel_;

    FutureSpawn(const FutureSpawn&) = delete;
    FutureSpawn& operator=(const FutureSpawn&) = delete;
};

class FutureSpawnRun : public Runnable {
public:
  class Inner: public Unpark {
  public:
    Inner(Executor *exec)
      : exec_(exec) {
    }

    void unpark() override {
      auto res = mu_.notify();
      if (res.hasValue()) {
        exec_->execute(std::move(res).value());
      }
    }

    Executor *exec_;
    UnparkMutex<std::unique_ptr<FutureSpawnRun>> mu_;
  };

  // run future to complete
  void run() override {
    inner_->mu_.start_poll();
    while (true) {
      Poll<folly::Unit> p = spawn_.poll_future(inner_.get());
      if (p.hasException()) {
        inner_->mu_.complete();
        return;
      } else {
        Async<folly::Unit> r = folly::moveFromTry(p);
        if (r.isReady()) {
          inner_->mu_.complete();
          return;
        } else {
          // schedule next run
          auto res = inner_->mu_.wait(
              folly::make_unique<FutureSpawnRun>(std::move(spawn_), inner_));
          if (res) {
            // retry
            spawn_ = std::move(std::move(res).value()->spawn_);
          } else {
            return;
          }
        }
      }
    }
  }

  FutureSpawnRun(Executor *exec, FutureSpawn<folly::Unit> spawn)
    : spawn_(std::move(spawn)), inner_(std::make_shared<Inner>(exec)) {
  }

  FutureSpawnRun(FutureSpawn<folly::Unit> spawn, std::shared_ptr<Inner> inner)
    : spawn_(std::move(spawn)), inner_(inner)
  {
  }

private:
  FutureSpawn<folly::Unit> spawn_;
  std::shared_ptr<Inner> inner_;

};

// Future impl

template <typename T>
template <typename F>
typename detail::argResult<false, F, T>::Result
Future<T>::andThen(F&& f) {
  typedef typename detail::argResult<false, F, T>::Result Result;
  typedef typename isFuture<Result>::Inner R;
  typedef std::function<Result(Try<T>)> FN;

  static_assert(isFuture<Result>::value, "andThen callback must returns Future");
  validFuture();
  return Result(folly::make_unique<ThenFutureImpl<T, FN, R>>(move_this(),
        [f] (Try<T> v) {
    if (v.hasValue()) {
      return f(moveFromTry(v));
    } else {
      return Result::err(v.exception());
    }
  }));
}

template <typename T>
template <typename F>
typename detail::argResult<false, F, Try<T>>::Result
Future<T>::then(F&& f) {
  typedef typename detail::argResult<false, F, T>::Result Result;
  typedef typename isFuture<Result>::Inner R;

  validFuture();
  return Result(folly::make_unique<ThenFutureImpl<T, F, R>>
      (move_this(), std::forward<F>(f)));
}

template <typename T>
Poll<T> Future<T>::wait() {
  validFuture();
  FutureSpawn<T> spawn(move_this());
  return spawn.wait_future();
}

}

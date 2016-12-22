#pragma once

#include <memory>
#include <iostream>
#include <futures/core/Memory.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>

#include <futures/Exception.h>
#include <futures/Async.h>
#include <futures/Executor.h>
#include <futures/Task.h>
#include <futures/UnparkMutex.h>
#include <futures/Future-pre.h>

namespace futures {

template <typename T>
class IFuture {
public:
    virtual Poll<T> poll() = 0;
    virtual ~IFuture() = default;
};

template <typename T, typename FutA, typename F> class ThenFuture;
template <typename T> class BoxedFuture;
// future or error
template <typename FutT> class MaybeFuture;

template <typename Derived, typename T>
class FutureBase : public IFuture<T> {
public:
    typedef T Item;

    Poll<T> poll() override {
        return derived().poll();
    }

    template <typename F,
              typename FutR = typename std::result_of<F(T)>::type,
              typename R = typename isFuture<FutR>::Inner,
              typename FN = std::function<MaybeFuture<FutR>(Try<T>)> >
    ThenFuture<R, Derived, FN> andThen(F&& f);

    template <typename F,
              typename R = typename isFuture<
                typename std::result_of<F(Try<T>)>::type>::Inner>
    ThenFuture<R, Derived, F> then(F&& f);

    BoxedFuture<T> boxed();

    Poll<T> wait();

    Async<T> value() {
      return wait().value();
    }

    FutureBase(const FutureBase &) = delete;
    FutureBase& operator=(const FutureBase &) = delete;

    FutureBase() = default;

    ~FutureBase() {
      // std::cerr << "Future destroy: " << folly::demangle(typeid(Derived)) << std::endl;
    }

#if DEBUG_FUTURE
    FutureBase(FutureBase &&o) {
      std::swap(__moved_mark, o.__moved_mark);
    }

    FutureBase& operator=(FutureBase &&o) {
      std::swap(__moved_mark, o.__moved_mark);
    }
#else
    FutureBase(FutureBase &&) = default;
    FutureBase& operator=(FutureBase &&) = default;
#endif
private:

#if DEBUG_FUTURE
    bool __moved_mark = false;
#endif

    Derived& derived()
    {
        return *static_cast<Derived*>(this);
    }

    Derived move_self() {
        return std::move(*static_cast<Derived*>(this));
    }
};

template <typename T>
class BoxedFuture : public FutureBase<BoxedFuture<T>, T> {
public:
    typedef T Item;

    Poll<T> poll() {
        validFuture();
        return impl_->poll();
    }

    BoxedFuture(std::unique_ptr<IFuture<T>> f)
        : impl_(std::move(f)) {}

    ~BoxedFuture() {
      if (impl_)
        std::cerr << "BOX DESTRY" << std::endl;
    }
    BoxedFuture(BoxedFuture&&) = default;
    BoxedFuture& operator=(BoxedFuture&&) = default;
private:
    std::unique_ptr<IFuture<T>> impl_;

    void validFuture() {
      if (!impl_) throw MovedFutureException();
    }
};

template <typename T>
class EmptyFuture : public FutureBase<EmptyFuture<T>, T> {
public:
    typedef T Item;
    Poll<T> poll() override {
        return Poll<T>(Async<T>());
    }
};

template <typename T>
class ErrFuture : public FutureBase<ErrFuture<T>, T> {
public:
    typedef T Item;
    Poll<T> poll() override {
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
        return Poll<T>(std::move(e_));
    }

    explicit ErrFuture(folly::exception_wrapper e)
        : consumed_(false), e_(std::move(e)) {
    }
private:
    bool consumed_;
    folly::exception_wrapper e_;
};

template <typename T>
class OkFuture : public FutureBase<OkFuture<T>, T> {
public:
    typedef T Item;
    Poll<T> poll() override {
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
        return Poll<T>(Async<T>(std::move(v_)));
    }

    explicit OkFuture(const T& v)
        : consumed_(false), v_(v) {
    }

    explicit OkFuture(T&& v) noexcept
        : consumed_(false), v_(std::move(v)) {
    }

private:
    bool consumed_;
    T v_;
};

template <typename T>
class ResultFuture: public FutureBase<ResultFuture<T>, T> {
public:
    typedef T Item;

    ResultFuture(Try<T> &&t)
      : try_(std::move(t)) {
    }

    Poll<T> poll() override {
      if (try_.hasException()) {
        return Poll<T>(try_.exception());
      } else {
        return Poll<T>(Async<T>(folly::moveFromTry(try_)));
      }
    }

private:
    Try<T> try_;
};

template <typename FutA>
class MaybeFuture: public FutureBase<MaybeFuture<FutA>, typename isFuture<FutA>::Inner> {
public:
    typedef typename isFuture<FutA>::Inner Item;

    explicit MaybeFuture(FutA fut)
      : try_(std::move(fut)) {
    }

    explicit MaybeFuture(folly::exception_wrapper ex)
      : try_(std::move(ex)) {
    }

    Poll<Item> poll() override {
      if (try_.hasException()) {
        return Poll<Item>(try_.exception());
      } else {
        return try_->poll();
      }
    }

private:
    Try<FutA> try_;
};


template <typename T, typename F>
class LazyFuture: public FutureBase<LazyFuture<T, F>, T> {
public:
    typedef T Item;
    LazyFuture(F &&fn)
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

template <typename FutA, typename F>
class ChainStateMachine {
    enum class State {
        First,
        Second,
        Done,
    };
public:
    static_assert(isFuture<FutA>::value, "chain callback must be future");
    typedef typename isFuture<FutA>::Inner AInner;
    typedef typename detail::argResult<false, F, Try<AInner>>::Result f_result;
    static_assert(isFuture<f_result>::value, "chain callback must returns Future");

    typedef typename f_result::Item b_type;
    typedef Poll<b_type> poll_type;

    ChainStateMachine(FutA a, F&& fn)
        : state_(State::First), a_(std::move(a)), handler_(std::move(fn)) {
    }

    poll_type poll() {
        switch (state_) {
            case State::First: {
                auto p = a_.poll();
                if (p.hasValue()) {
                  auto v = folly::moveFromTry(p);
                  if (v.isReady()) {
                    return poll_second(Try<AInner>(std::move(v.value())));
                  } else {
                    return poll_type(Async<b_type>());
                  }
                } else {
                    return poll_second(Try<AInner>(p.exception()));
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
    poll_type poll_second(Try<AInner>&& a_result) {
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
    FutA a_;
    folly::Optional<f_result> b_;
    F handler_;
};

template <typename T, typename FutA, typename F>
class ThenFuture: public FutureBase<ThenFuture<T, FutA, F>, T> {
public:
  typedef T Item;

  static_assert(isFuture<FutA>::value, "must be future");

  Poll<T> poll() override {
    return state_.poll();
  }

  ThenFuture(FutA a, F&& f)
    : state_(std::move(a), std::move(f)) {
  }

private:
  ChainStateMachine<FutA, F> state_;
};

// fused Task & Future
template <typename Fut>
class FutureSpawn {
public:
    typedef typename isFuture<Fut>::Inner T;
    typedef Try<Async<T>> poll_type;

    poll_type poll_future(std::shared_ptr<Unpark> unpark) {
        Task task(id_, unpark);

        CurrentTask::WithGuard g(CurrentTask::this_thread(), &task);
        return toplevel_.poll();
    }

    poll_type wait_future() {
        auto unpark = std::make_shared<ThreadUnpark>();
        while (true) {
            auto r = poll_future(unpark);
            if (r.hasException())
                return r;
            auto async = folly::moveFromTry(r);
            if (async.isReady()) {
                return poll_type(std::move(async));
            } else {
                unpark->park();
            }
        }
    }

    explicit FutureSpawn(Fut fut)
      : id_(detail::newTaskId()), toplevel_(std::move(fut)) {
    }

    FutureSpawn(FutureSpawn&&) = default;
    FutureSpawn& operator=(FutureSpawn&&) = default;
private:
    // toplevel future or stream
    unsigned long id_;
    Fut toplevel_;

    FutureSpawn(const FutureSpawn&) = delete;
    FutureSpawn& operator=(const FutureSpawn&) = delete;
};

class FutureSpawnRun : public Runnable {
public:
  typedef FutureSpawn<BoxedFuture<folly::Unit>> spawn_type;

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

    ~Inner() {
      std::cerr << "INNER DES" << std::endl;
    }

    Executor *exec_;
    UnparkMutex<std::unique_ptr<FutureSpawnRun>> mu_;
  };

  // run future to complete
  void run() override {
    inner_->mu_.start_poll();
    while (true) {
      Poll<folly::Unit> p = spawn_.poll_future(inner_);
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

  FutureSpawnRun(Executor *exec, spawn_type spawn)
    : spawn_(std::move(spawn)), inner_(std::make_shared<Inner>(exec)) {
  }

  FutureSpawnRun(spawn_type spawn, std::shared_ptr<Inner> inner)
    : spawn_(std::move(spawn)), inner_(inner)
  {
  }

private:
  spawn_type spawn_;
  std::shared_ptr<Inner> inner_;

};

// helper

template <typename T>
OkFuture<T> makeOk(T &&v) {
  return OkFuture<T>(std::forward<T>(v));
}

template <typename T>
OkFuture<T> makeOk(const T &v) {
  return OkFuture<T>(v);
}

static inline OkFuture<folly::Unit> makeOk() {
  return OkFuture<folly::Unit>(folly::Unit());
}

template <typename T>
EmptyFuture<T> makeEmpty() {
  return EmptyFuture<T>();
}

}

#include <futures/Future-inl.h>

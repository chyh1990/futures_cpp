#pragma once

#include <memory>
#include <iostream>

#include <futures/Core.h>
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
    // virtual void cancel() {}
    virtual ~IFuture() = default;
};

template <typename T, typename FutA, typename F> class ThenFuture;
template <typename FutA, typename FutB> class JoinFuture;
template <typename T> class BoxedFuture;
template <typename T> class SharedFuture;
// future or error
template <typename FutT> class MaybeFuture;

template <typename T, typename F>
struct AndThenWrapper;
template <typename T, typename F>
struct AndThenWrapper2;
template <typename T, typename F>
struct ErrorWrapper;

template <typename Derived, typename T>
class FutureBase : public IFuture<T> {
public:
    using Item = T;

    Poll<T> poll() override {
        // return derived().poll();
        assert(0 && "cannot call base poll");
    }

    template <typename F,
              typename FutR = detail::resultOf<F, T>,
              typename R = typename isFuture<FutR>::Inner,
              typename Wrapper = AndThenWrapper<T, F>>
    ThenFuture<R, Derived, Wrapper> andThen(F&& f);

    template <typename F,
              typename FutR = decltype(folly::applyTuple(std::declval<F>(), std::declval<T>())),
              typename R = typename isFuture<FutR>::Inner,
              typename Wrapper = AndThenWrapper2<T, F>>
    ThenFuture<R, Derived, Wrapper> andThen2(F&& f);

    template <typename F,
              typename R = typename isFuture<
                detail::resultOf<F,Try<T>> >::Inner>
    ThenFuture<R, Derived, F> then(F&& f);

    template <typename F, typename Wrapper = ErrorWrapper<T, F>>
    ThenFuture<folly::Unit, Derived, Wrapper> error(F&& f);

    template <typename FutB>
    JoinFuture<Derived, FutB> join(FutB&& f);

    BoxedFuture<T> boxed();
    SharedFuture<T> shared();

    Poll<T> wait();

    Async<T> value() {
      return wait().value();
    }

    FutureBase(const FutureBase &) = delete;
    FutureBase& operator=(const FutureBase &) = delete;

    FutureBase() = default;
    ~FutureBase() = default;

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
protected:

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
    using Item = T;

    Poll<T> poll() {
        validFuture();
        return impl_->poll();
    }

    void clear() { impl_.reset(); }
    bool isValid() { return impl_ != nullptr; }

    explicit BoxedFuture(std::unique_ptr<IFuture<T>> f)
        : impl_(std::move(f)) {}

    ~BoxedFuture() {
    }

    // override should be safe
    BoxedFuture<T> boxed() {
      return BoxedFuture<T>(std::move(impl_));
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
    using Item = T;
    Poll<T> poll() override {
        return Poll<T>(Async<T>());
    }
};

template <typename T>
class ErrFuture : public FutureBase<ErrFuture<T>, T> {
public:
    using Item = T;
    Poll<T> poll() override {
#if DEBUG_FUTURE
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
#endif
        return Poll<T>(std::move(e_));
    }

    explicit ErrFuture(folly::exception_wrapper e)
        : e_(std::move(e)) {
    }
private:
#if DEBUG_FUTURE
    bool consumed_ = false;
#endif
    folly::exception_wrapper e_;
};

template <typename T>
class OkFuture : public FutureBase<OkFuture<T>, T> {
public:
    using Item = T;
    Poll<T> poll() override {
#if DEBUG_FUTURE
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
#endif
        return Poll<T>(Async<T>(std::move(v_)));
    }

    explicit OkFuture(const T& v)
        : v_(v) {
    }

    explicit OkFuture(T&& v) noexcept
        : v_(std::move(v)) {
    }

private:
#if DEBUG_FUTURE
    bool consumed_ = false;
#endif
    T v_;
};

template <typename T>
class ResultFuture: public FutureBase<ResultFuture<T>, T> {
public:
    using Item = T;

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
    using Item = typename isFuture<FutA>::Inner;

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
        auto r = try_->poll();
        if (r.hasException() || r->isReady())
          try_ = Try<FutA>();
        return r;
      }
    }

private:
    Try<FutA> try_;
};


template <typename T, typename F>
class LazyFuture: public FutureBase<LazyFuture<T, F>, T> {
public:
    using Item = T;
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

// fused Task & Future
template <typename Fut>
class FutureSpawn {
public:
    using T = typename isFuture<Fut>::Inner;
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
  using spawn_type = FutureSpawn<BoxedFuture<folly::Unit>>;

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
      FUTURES_DLOG(INFO) << "FutureSpawn INNER DESTROY";
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

static inline OkFuture<folly::Unit> makeOk() {
  return OkFuture<folly::Unit>(folly::Unit());
}

template <typename T>
EmptyFuture<T> makeEmpty() {
  return EmptyFuture<T>();
}

template <typename F,
         typename Return = typename std::remove_reference<detail::resultOf<F>>::type>
LazyFuture<Return, F> makeLazy(F&& f) {
  return LazyFuture<Return, F>(std::forward<F>(f));
}

}

#include <futures/Future-inl.h>
#include <futures/detail/SelectFuture.h>
#include <futures/detail/WhenAllFuture.h>
#include <futures/detail/LoopFn.h>

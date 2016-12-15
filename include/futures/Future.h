#pragma once

#include <memory>
#include <iostream>
#include <futures/core/Memory.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>

#include <futures/Async.h>
#include <futures/Executor.h>
#include <futures/Task.h>
#include <futures/Future-pre.h>

namespace futures {

class InvalidPollStateException : public std::runtime_error {
 public:
  InvalidPollStateException()
      : std::runtime_error("Cannot poll twice") {}
};

template <typename T>
class FutureImpl {
public:
    typedef T item_type;
    typedef Try<Async<T>> poll_type;

    virtual poll_type poll() = 0;
    virtual ~FutureImpl() = default;
};

template <typename T>
class EmptyFutureImpl : public FutureImpl<T> {
public:
    typedef typename FutureImpl<T>::poll_type poll_type;

    poll_type poll() override {
        return poll_type(Async<T>());
    }
};

template <typename T>
class ErrFutureImpl : public FutureImpl<T> {
public:
    typedef typename FutureImpl<T>::poll_type poll_type;

    poll_type poll() override {
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
        return poll_type(std::move(e_));
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
    typedef typename FutureImpl<T>::poll_type poll_type;

    poll_type poll() override {
        if (consumed_)
            throw InvalidPollStateException();
        consumed_ = true;
        return poll_type(Async<T>(std::move(v_)));
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

template <typename T>
class Future {
public:
    typedef T item_type;

    Try<Async<item_type>> poll() {
        return impl_->poll();
    };

    static Future<T> empty() {
        return Future<T>(new EmptyFutureImpl<T>());
    }

    static Future<T> err(folly::exception_wrapper e) {
        return Future<T>(new ErrFutureImpl<T>(e));
    }

    // move & copy?
    static Future<T> ok(T&& v) {
        return Future<T>(new OkFutureImpl<T>(std::move(v)));
    }

    static Future<T> ok(const T& v) {
        return Future<T>(new OkFutureImpl<T>(v));
    }

    template <typename F>
    typename detail::argResult<false, F, T>::Result
    andThen(F&& f);

    template <typename F>
    typename detail::argResult<false, F, Try<T>>::Result
    then(F&& f);

    ~Future() {
      std::cerr << impl_.get() << " " << impl_.use_count() << std::endl;
    }

    Future(FutureImpl<T> *impl) : impl_(impl) {
      std::cerr << impl_.get() << std::endl;
    }

    Try<Async<T>> wait();

    Async<T> value() {
      return wait().value();
    }

private:
    std::shared_ptr<FutureImpl<T>> impl_;
};

// fused Task & Future
template <typename T>
class FutureSpawn : public Runnable {
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

    void run() override {
        // do something
    }

    FutureSpawn(Future<T> f)
      : id_(detail::newTaskId()), toplevel_(f) {
        std::cerr << "BBB"<<std::endl;
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
        : state_(State::First), a_(a), handler_(std::move(fn)) {
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
ChainStateMachine<A, F> makeChain(Future<A> a, F&& f) {
    return ChainStateMachine<A, F>(a, std::forward<F>(f));
}

template <typename T, typename F, typename R>
class ThenFutureImpl: public FutureImpl<R> {
public:
  typedef typename ChainStateMachine<T, F>::poll_type poll_type;

  poll_type poll() override {
    return state_.poll();
  }

  ThenFutureImpl(Future<T> a, F&& f)
    : state_(a, std::move(f)) {
  }

private:
  ChainStateMachine<T, F> state_;
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
  return Result(new ThenFutureImpl<T, FN, R>(*this, [f] (Try<T> v) {
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

  return Result(new ThenFutureImpl<T, F, R>(*this, std::forward<F>(f)));
}

template <typename T>
Try<Async<T>> Future<T>::wait() {
  FutureSpawn<T> spawn(*this);
  return spawn.wait_future();
}

}

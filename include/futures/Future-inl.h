#include <futures/Future.h>
#include <futures/core/Either.h>

namespace futures {

// Future impl
template <typename FutA, typename F>
class ChainStateMachine {
    enum class State {
        First,
        Second,
        Done,
    };
public:
    static_assert(isFuture<FutA>::value, "chain callback must be future");
    using AInner = typename isFuture<FutA>::Inner;
    using f_result = typename detail::argResult<false, F, Try<AInner>>::Result;
    static_assert(isFuture<f_result>::value, "chain callback must returns Future");

    using b_type = typename f_result::Item;
    using poll_type = Poll<b_type>;

    ChainStateMachine(FutA&& a, F&& fn)
        : state_(State::First),
        v_(folly::left_tag, std::make_pair(std::move(a), std::move(fn))) {
    }

    poll_type poll() {
        switch (state_) {
            case State::First: {
                assert(v_.hasLeft());
                auto p = v_.left().first.poll();
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
                assert(v_.hasRight());
                return v_.right().poll();
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
          v_.assignRight(v_.left().second(std::move(a_result)));
          return v_.right().poll();
        } catch (const std::exception &e) {
          return poll_type(folly::exception_wrapper(std::current_exception(), e));
        }
    }

    State state_;
    folly::Either<std::pair<FutA, F>, f_result> v_;
    // FutA a_;
    // F handler_;
    // folly::Optional<f_result> b_;
};

template <typename T, typename FutA, typename F>
class ThenFuture: public FutureBase<ThenFuture<T, FutA, F>, T> {
public:
  using Item = T;

  static_assert(isFuture<FutA>::value, "must be future");

  Poll<T> poll() override {
    return state_.poll();
  }

  ThenFuture(FutA&& a, F&& f)
    : state_(std::move(a), std::move(f)) {
  }

private:
  ChainStateMachine<FutA, F> state_;
};


template <typename U, typename V>
using JoinFutureItem = std::tuple<typename isFuture<U>::Inner,
      typename isFuture<V>::Inner>;

template <typename FutA, typename FutB>
class JoinFuture: public FutureBase<JoinFuture<FutA, FutB>,
  JoinFutureItem<FutA,FutB>> {
public:
  using Item = JoinFutureItem<FutA,FutB>;
  using AInner = typename isFuture<FutA>::Inner;
  using BInner = typename isFuture<FutB>::Inner;

  JoinFuture(FutA&& fa, FutB&& fb)
    : fa_(folly::left_tag, std::move(fa)), fb_(folly::left_tag, std::move(fb)) {
  }

  Poll<Item> poll() override {
    if (fa_.hasLeft()) {
      auto r = fa_.left().poll();
      if (r.hasException())
        return Poll<Item>(r.exception());
      auto ra = folly::moveFromTry(r);
      if (!ra.isReady())
        return Poll<Item>(not_ready);
      fa_.assignRight(std::move(ra).value());
    }
    if (fb_.hasLeft()) {
      auto r = fb_.left().poll();
      if (r.hasException())
        return Poll<Item>(r.exception());
      auto rb = folly::moveFromTry(r);
      if (!rb.isReady())
        return Poll<Item>(not_ready);
      fb_.assignRight(std::move(rb).value());
    }
    assert(fa_.hasRight() && fb_.hasRight());
    return makePollReady(
          std::make_tuple(std::move(fa_).right(), std::move(fb_).right())
      );
  }

private:
  folly::Either<FutA, AInner> fa_;
  folly::Either<FutB, BInner> fb_;
  // Optional<AInner> ra_;
  // Optional<BInner> rb_;
};

template <typename T, typename F>
struct AndThenWrapper {
  using FutR = typename std::result_of<F(T)>::type;
  AndThenWrapper(F&& f)
    : func_(std::forward<F>(f)) {
  }

  MaybeFuture<FutR> operator()(Try<T> v) {
    if (v.hasValue()) {
      return MaybeFuture<FutR>(func_(moveFromTry(v)));
    } else {
      return MaybeFuture<FutR>(v.exception());
    }
  }
  F func_;
};

template <typename T, typename F>
struct AndThenWrapper2 {
  using FutR = decltype(folly::applyTuple(std::declval<F>(), std::declval<T>()));
  AndThenWrapper2(F&& f)
    : func_(std::forward<F>(f)) {
  }

  MaybeFuture<FutR> operator()(Try<T> v) {
    if (v.hasValue()) {
      return MaybeFuture<FutR>(folly::applyTuple(func_, folly::moveFromTry(v)));
    } else {
      return MaybeFuture<FutR>(v.exception());
    }
  }
  F func_;
};

template <typename T, typename F>
struct ErrorWrapper {
  ErrorWrapper(F&& f)
    : func_(std::forward<F>(f)) {
  }

  OkFuture<folly::Unit> operator()(Try<T> v) {
    if (v.hasException())
      func_(v.exception());
    return makeOk();
  }

  F func_;
};

template <typename T>
class SharedFuture : public FutureBase<SharedFuture<T>, T> {
public:
    using Item = T;
    typedef std::unique_ptr<IFuture<T>> fptr;

    static_assert(std::is_copy_constructible<T>::value, "T must be copyable");

    // TODO use finer grain lock
    struct Inner {
      std::mutex mu;
      fptr impl;
      std::vector<Task> waiters;
      Optional<Poll<Item>> result;

      Inner(fptr f): impl(std::move(f)) {}
    };

    // TODO optimized out allocation
    explicit SharedFuture(fptr impl)
      : inner_(std::make_shared<Inner>(std::move(impl))) {}

    SharedFuture(const SharedFuture& o)
      : inner_(o.inner_) {
#ifdef DEBUG_FUTURE
      __moved_mark = o.__moved_mark;
#endif
    }
    SharedFuture& operator=(const SharedFuture& o) {
#ifdef DEBUG_FUTURE
      __moved_mark = o.__moved_mark;
#endif
      inner_ = o.inner;
      return *this;
    }

    // TODO allow polling from multiple source
    Poll<Item> poll() {
      if (!inner_) throw InvalidPollStateException();
      std::lock_guard<std::mutex> g(inner_->mu);

      if (inner_->result)
        return inner_->result.value();
      auto r = inner_->impl->poll();
      if (r.hasException() || r.value().isReady()) {
        inner_->result = r;
        inner_->impl.reset(); // release original future
        unpark_all();
        return std::move(r);
      }
      park();
      return Poll<Item>(not_ready);
    }

    ~SharedFuture() {
      if (!inner_) return;
      std::lock_guard<std::mutex> g(inner_->mu);
      if (inner_->result) return;
      // wakeup another tasks, one of them will go on polling
      // the original future.
      unpark_all();
    }

    SharedFuture(SharedFuture&&) = default;
    SharedFuture& operator=(SharedFuture&&) = default;
private:
    std::shared_ptr<Inner> inner_;

    void park() {
      inner_->waiters.push_back(*CurrentTask::current());
    }

    void unpark_all() {
      for (auto &e : inner_->waiters)
        e.unpark();
      inner_->waiters.clear();
    }
};

template <typename Derived, typename T>
template <typename F, typename FutR,
          typename R, typename Wrapper>
ThenFuture<R, Derived, Wrapper> FutureBase<Derived, T>::andThen(F&& f) {
  static_assert(isFuture<FutR>::value, "andThen callback must returns Future");
  return ThenFuture<R, Derived, Wrapper>(move_self(), Wrapper(std::forward<F>(f)));
}

template <typename Derived, typename T>
template <typename F, typename FutR,
          typename R, typename Wrapper>
ThenFuture<R, Derived, Wrapper> FutureBase<Derived, T>::andThen2(F&& f) {
  static_assert(isFuture<FutR>::value, "andThen2 callback must returns Future");
  return ThenFuture<R, Derived, Wrapper>(move_self(), Wrapper(std::forward<F>(f)));
}

template <typename Derived, typename T>
template <typename F, typename R>
ThenFuture<R, Derived, F> FutureBase<Derived, T>::then(F&& f) {
  return ThenFuture<R, Derived, F>(move_self(), std::forward<F>(f));
}

template <typename Derived, typename T>
template <typename F, typename Wrapper>
ThenFuture<folly::Unit, Derived, Wrapper> FutureBase<Derived, T>::error(F&& f) {
  return ThenFuture<folly::Unit, Derived, Wrapper>(move_self(), Wrapper(std::forward<F>(f)));
}

template <typename Derived, typename T>
template <typename FutB>
JoinFuture<Derived, FutB> FutureBase<Derived, T>::join(FutB&& f) {
  return JoinFuture<Derived, FutB>(move_self(), std::forward<FutB>(f));
}

template <typename Derived, typename T>
Poll<T> FutureBase<Derived, T>::wait() {
  FutureSpawn<Derived> spawn(move_self());
  return spawn.wait_future();
}

template <typename Derived, typename T>
BoxedFuture<T> FutureBase<Derived, T>::boxed() {
  std::unique_ptr<IFuture<T>> p(new Derived(move_self()));
#ifdef FUTURES_ENABLE_DEBUG_PRINT
  FUTURES_DLOG(INFO) << "Future boxed: " << p.get() << typeid(Derived).name()
     << ", size: " << sizeof(Derived);
#endif
  return BoxedFuture<T>(std::move(p));
}

template <typename Derived, typename T>
SharedFuture<T> FutureBase<Derived, T>::shared() {
  std::unique_ptr<IFuture<T>> p(new Derived(move_self()));
  return SharedFuture<T>(std::move(p));
}


}

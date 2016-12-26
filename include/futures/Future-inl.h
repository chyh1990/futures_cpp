#include <futures/Future.h>

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
  using Item = T;

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

  JoinFuture(FutA fa, FutB fb)
    : fa_(std::move(fa)), fb_(std::move(fb)) {
  }

  Poll<Item> poll() override {
    if (!ra_.hasValue()) {
      auto r = fa_.poll();
      if (r.hasException())
        return Poll<Item>(r.exception());
      auto ra = folly::moveFromTry(r);
      if (!ra.isReady())
        return Poll<Item>(not_ready);
      ra_ = std::move(ra).value();
    }
    if (!rb_.hasValue()) {
      auto r = fb_.poll();
      if (r.hasException())
        return Poll<Item>(r.exception());
      auto rb = folly::moveFromTry(r);
      if (!rb.isReady())
        return Poll<Item>(not_ready);
      rb_ = std::move(rb).value();
    }
    assert(ra_.hasValue() && rb_.hasValue());
    return Poll<Item>(Async<Item>(
          std::make_tuple(std::move(ra_).value(), std::move(rb_).value())
      ));
  }

private:
  FutA fa_;
  FutB fb_;
  Optional<AInner> ra_;
  Optional<BInner> rb_;
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
  return BoxedFuture<T>(std::move(p));
}

template <typename Derived, typename T>
SharedFuture<T> FutureBase<Derived, T>::shared() {
  std::unique_ptr<IFuture<T>> p(new Derived(move_self()));
  return SharedFuture<T>(std::move(p));
}


}

#include <futures/Future.h>

namespace futures {

// Future impl

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

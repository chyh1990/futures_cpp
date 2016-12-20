#include <futures/Future.h>

namespace futures {

// Future impl

template <typename Derived, typename T>
template <typename F, typename FutR,
          typename R, typename FN>
ThenFuture<R, Derived, FN> FutureBase<Derived, T>::andThen(F&& f) {
  static_assert(isFuture<FutR>::value, "andThen callback must returns Future");
  return ThenFuture<R, Derived, FN>(move_self(), [f] (Try<T> v) {
    if (v.hasValue()) {
      return MaybeFuture<FutR>(f(moveFromTry(v)));
    } else {
      return MaybeFuture<FutR>(v.exception());
    }
  });
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

}

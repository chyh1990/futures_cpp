#pragma once

// included by Future.h, do not include directly.
#include <type_traits>
#include <futures/Core.h>

namespace futures {

template <typename T>
class IFuture;

template <typename Derived, typename T>
class FutureBase;

template <class, class> class Promise;

template <typename T, typename Inner>
using isPollable = std::is_base_of<IFuture<Inner>, T>;

// // template <typename T>
// struct isFuture< : std::true_type {
//   using Inner = typename folly::Unit::Lift<T>::type;
// };
template <typename T>
struct isFuture {
  using Inner = typename folly::Unit::Lift<typename T::Item>::type;
  static const bool value = isPollable<T, Inner>::value;
};

namespace detail {

template <bool isTry, typename F, typename... Args>
struct argResult {
  using Result = resultOf<F, Args...>;
};

template<typename F, typename... Args>
struct callableWith {
    template<typename T,
             typename = detail::resultOf<T, Args...>>
    static constexpr std::true_type
    check(std::nullptr_t) { return std::true_type{}; };

    template<typename>
    static constexpr std::false_type
    check(...) { return std::false_type{}; };

    typedef decltype(check<F>(nullptr)) type;
    static constexpr bool value = type::value;
};

template<typename T, typename F>
struct callableResult {
  typedef typename std::conditional<
    callableWith<F>::value,
    detail::argResult<false, F>,
    typename std::conditional<
      callableWith<F, T&&>::value,
      detail::argResult<false, F, T&&>,
      typename std::conditional<
        callableWith<F, T&>::value,
        detail::argResult<false, F, T&>,
        typename std::conditional<
          callableWith<F, folly::Try<T>&&>::value,
          detail::argResult<true, F, folly::Try<T>&&>,
          detail::argResult<true, F, folly::Try<T>&>>::type>::type>::type>::type Arg;
//  typedef isFuture<typename Arg::Result> ReturnsFuture;
//  typedef Future<typename ReturnsFuture::Inner> Return;
};

}

}

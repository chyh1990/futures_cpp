#pragma once

#include <futures/core/Memory.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>
#include <futures/core/ApplyTuple.h>
#include <futures/core/MoveWrapper.h>

namespace futures {

template <typename T>
struct isTry : std::false_type {};

template <typename T>
struct isTry<folly::Try<T>> : std::true_type {};

namespace detail {

template<typename F, typename... Args>
using resultOf = decltype(std::declval<F>()(std::declval<Args>()...));

template <typename...>
struct ArgType;

template <typename Arg, typename... Args>
struct ArgType<Arg, Args...> {
  typedef Arg FirstArg;
};

template <>
struct ArgType<> {
  typedef void FirstArg;
};

}
}

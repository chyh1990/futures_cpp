#pragma once

#include <futures/Future.h>

namespace futures {

namespace detail {
template <typename T>
    struct get_arity : get_arity<decltype(&T::operator())> {};
template <typename R, typename... Args>
    struct get_arity<R(*)(Args...)> : std::integral_constant<unsigned, sizeof...(Args)> {};
template <typename R, typename C, typename... Args>
    struct get_arity<R(C::*)(Args...)> : std::integral_constant<unsigned, sizeof...(Args)> {};
template <typename R, typename C, typename... Args>
    struct get_arity<R(C::*)(Args...) const> : std::integral_constant<unsigned, sizeof...(Args)> {};
}

// '+' join
template <typename Fut, typename F>
auto operator+(Fut&& fut, F&& f)
    -> decltype(
            std::forward<Fut>(fut)
                .join(std::forward<F>(f)))
{
    return std::forward<Fut>(fut)
        .join(std::forward<F>(f));
}

// '>>' andThen
template <typename Fut, typename F>
auto operator>>(Fut&& fut, F&& f)
    -> decltype(
            std::forward<Fut>(fut)
                .andThen(std::forward<F>(f)))
{
    return std::forward<Fut>(fut)
        .andThen(std::forward<F>(f));
}

template <typename Fut, typename F>
auto operator >>(Fut&& fut, F&& f)
    -> decltype(
            std::forward<Fut>(fut)
                .andThen2(std::forward<F>(f)))
{
    return std::forward<Fut>(fut)
        .andThen2(std::forward<F>(f));
}

// '<<' andThen
template <typename Fut, typename F>
auto operator <<(Fut&& fut, F&& f)
    -> decltype(
            std::forward<Fut>(fut)
                .then(std::forward<F>(f)))
{
    return std::forward<Fut>(fut)
        .then(std::forward<F>(f));
}

// '|' map
template <typename Fut, typename F>
auto operator|(Fut&& fut, F&& f)
    -> decltype(
            std::forward<Fut>(fut)
                .map(std::forward<F>(f)))
{
    return std::forward<Fut>(fut)
        .map(std::forward<F>(f));
}

}

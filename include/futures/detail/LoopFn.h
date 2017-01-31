#pragma once

#include <futures/Future.h>
#include <futures/core/Either.h>

namespace futures {

template <typename B, typename C>
using LoopControl = folly::Either<B, C>;

template <typename T, typename S, typename F>
class LoopFnFuture : public FutureBase<LoopFnFuture<T, S, F>, T> {
public:
    using Item = T;
    using f_result = typename detail::argResult<false, F, S>::Result;
    // static_assert(isFuture<f_result>::value, "must return future");

    LoopFnFuture(S&& init, F&& f)
        : func_(std::move(f)), fut_(func_(std::forward<S>(init)))
    {
    }

    Poll<Item> poll() override {
        while (true) {
            auto r = fut_->poll();
            if (r.hasException())
                return Poll<Item>(r.exception());
            if (!r->isReady())
                return Poll<Item>(not_ready);
            auto v = folly::moveFromTry(r);
            if (v->hasLeft()) {
                fut_.clear();
                return makePollReady(std::move(v->left()));
            } else {
                fut_ = func_(std::move(v->right()));
            }
        }
    }
private:
    F func_;
    Optional<f_result> fut_;
};

template <typename S, typename F,
         typename T = typename isFuture<typename detail::argResult<false, F, S>::Result>::Inner::left_type>
LoopFnFuture<T, S, F> makeLoop(S&& s, F&& f) {
    return LoopFnFuture<T, S, F>(std::forward<S>(s), std::forward<F>(f));
}

template <typename B, typename C, typename T>
LoopControl<B, C> makeBreak(T&& v) {
    return folly::make_left<B, C>(std::forward<T>(v));
}

template <typename B, typename C, typename T>
LoopControl<B, C> makeContinue(T&& v) {
    return folly::make_right<B, C>(std::forward<T>(v));
}


}

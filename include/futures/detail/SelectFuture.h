#pragma once

#include <futures/Future.h>

namespace futures {

template <typename T, typename FutA, typename FutB>
class SelectNextFuture : public FutureBase<SelectNextFuture<T, FutA, FutB>, T>
{
public:
    static_assert(std::is_same<T, typename isFuture<FutA>::Inner>::value, "FutA return type mismatch");
    static_assert(std::is_same<T, typename isFuture<FutB>::Inner>::value, "FutB return type mismatch");
    typedef T Item;

    SelectNextFuture(Optional<FutA> fa, Optional<FutB> fb)
        : fa_(std::move(fa)), fb_(std::move(fb)) {
        assert((!fa.hasValue() && fb.hasValue())
            || (fa.hasValue() && !fb.hasValue()));
    }

    Poll<Item> poll() {
        if (fa_.hasValue())
            return fa_->poll();
        if (fb_.hasValue())
            return fb_->poll();
        throw InvalidPollStateException();
    }

private:
    // TODO optme
    Optional<FutA> fa_;
    Optional<FutB> fb_;
};

template <typename T>
using SelectFutureItem = std::pair<Try<T>, int>;

template <typename T, typename FutA, typename FutB>
class SelectFuture : public FutureBase<SelectFuture<T, FutA, FutB>, SelectFutureItem<T>> {
public:
    static_assert(std::is_same<T, typename isFuture<FutA>::Inner>::value, "FutA return type mismatch");
    static_assert(std::is_same<T, typename isFuture<FutB>::Inner>::value, "FutB return type mismatch");

    typedef SelectFutureItem<T> Item;

    SelectFuture(FutA fa, FutB fb)
        : fa_(std::move(fa)), fb_(std::move(fb)) {
    }

    Poll<Item> poll() {
        if (polled_)
            throw InvalidPollStateException();
        auto ra = fa_.poll();
        bool is_a = true;
        if (ra.hasException())
            return Poll<Item>(Async<Item>(std::make_pair(Try<T>(ra.exception()), 0)));
        auto va = folly::moveFromTry(ra);
        if (va.isReady())
            return Poll<Item>(Async<Item>(std::make_pair(Try<T>(std::move(va).value()), 0)));

        auto rb = fb_.poll();
        if (rb.hasException())
            return Poll<Item>(Async<Item>(std::make_pair(Try<T>(rb.exception()), 1)));

        auto vb = folly::moveFromTry(rb);
        if (vb.isReady()) {
            return Poll<Item>(Async<Item>(std::make_pair(Try<T>(std::move(vb).value()), 0)));
        } else {
            return Poll<Item>(not_ready);
        }
    }
private:
    bool polled_ = false;
    FutA fa_;
    FutB fb_;
};

template <typename FutA, typename FutB,
         typename R=typename isFuture<FutA>::Inner>
         SelectFuture<R, FutA, FutB> makeSelect(FutA fa, FutB fb) {
             return SelectFuture<R, FutA, FutB>(std::move(fa), std::move(fb));
         }


}

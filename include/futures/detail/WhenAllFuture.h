#pragma once

#include <futures/Future.h>
#include <vector>
#include <futures/core/Either.h>

namespace futures {

template <typename Fut>
using WhenAllItem = std::vector<typename isFuture<Fut>::Inner>;

template <typename Fut>
class WhenAllFuture: public FutureBase<WhenAllFuture<Fut>, WhenAllItem<Fut>> {
public:
    using Item = WhenAllItem<Fut>;
    using value_type = typename isFuture<Fut>::Inner;

    template <class Iterator>
    WhenAllFuture(Iterator begin, Iterator end) {
        if (begin == end) throw FutureEmptySetException();
        for (auto it = begin; it != end; ++it) {
            all_.emplace_back(folly::left_tag, std::move(*it));
        }
    }

    WhenAllFuture(std::vector<Fut> &&futs) {
        for (auto it = futs.begin(); it != futs.end(); ++it)
            all_.emplace_back(folly::left_tag, std::move(*it));
    }

    Poll<Item> poll() {
        bool all_done = true;
        for (size_t i = 0; i < all_.size(); ++i) {
            if (!all_[i].hasLeft()) continue;
            auto r = all_[i].left().poll();
            if (r.hasException()) {
                all_.clear();
                return Poll<Item>(r.exception());
            }
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                all_[i].assignRight(std::move(v).value());
            } else {
                all_done = false;
            }
        }
        if (all_done) {
            std::vector<value_type> vs;
            vs.reserve(all_.size());
            for (size_t i = 0; i < all_.size(); ++i)
                vs[i] = std::move(all_[i]).right();
            all_.clear();
            return makePollReady(std::move(vs));
        } else {
            return Poll<Item>(not_ready);
        }
    }

private:
    std::vector<folly::Either<Fut, value_type>> all_;
};

template <typename It,
         typename Fut = typename std::iterator_traits<It>::value_type,
         typename = typename isFuture<Fut>::Inner >
WhenAllFuture<Fut> makeWhenAll(It begin, It end) {
    return WhenAllFuture<Fut>(begin, end);
}

}

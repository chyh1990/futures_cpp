#pragma once

#include <futures/Future.h>
#include <vector>

namespace futures {

template <typename Fut>
using WhenAllItem = std::vector<typename isFuture<Fut>::Inner>;

template <typename Fut>
class WhenAllFuture: public FutureBase<WhenAllFuture<Fut>, WhenAllItem<Fut>> {
public:
    using Item = WhenAllItem<Fut>;
    using value_type = typename isFuture<Fut>::Inner;
    static_assert(std::is_default_constructible<value_type>::value, "T must be default constructiable");

    template <class Iterator>
    WhenAllFuture(Iterator begin, Iterator end) {
        std::move(begin, end, std::back_inserter(all_));
        if (all_.empty()) throw FutureEmptySetException();
        result_.resize(all_.size());
    }

    Poll<Item> poll() {
        bool all_done = true;
        for (size_t i = 0; i < all_.size(); ++i) {
            if (!all_[i].isValid()) continue;
            auto r = all_[i].poll();
            if (r.hasException()) {
                cancelPending();
                return Poll<Item>(r.exception());
            }
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                result_.emplace(result_.begin() + i, std::move(v).value());
                all_[i].clear();
            } else {
                all_done = false;
            }
        }
        if (all_done) {
            return Poll<Item>(Async<Item>(std::move(result_)));
        } else {
            return Poll<Item>(not_ready);
        }
    }

private:
    std::vector<Fut> all_;
    std::vector<value_type> result_;

    void cancelPending() {
        for (auto &e: all_)
            if (e.isValid()) e.cancel();
    }
};

template <typename It,
         typename Fut = typename std::iterator_traits<It>::value_type,
         typename = typename isFuture<Fut>::Inner >
WhenAllFuture<Fut> whenAll(It begin, It end) {
    return WhenAllFuture<Fut>(begin, end);
}

}

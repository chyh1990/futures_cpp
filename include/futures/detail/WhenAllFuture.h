#pragma once

#include <futures/Future.h>
#include <vector>

namespace futures {

template <typename T>
using WhenAllItem = std::vector<T>;

template <typename T>
class WhenAllFuture: public FutureBase<WhenAllFuture<T>, WhenAllItem<T>> {
public:
    using Item = WhenAllItem<T>;
    static_assert(std::is_default_constructible<T>::value, "T must be default constructiable");

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
    std::vector<BoxedFuture<T>> all_;
    std::vector<T> result_;

    void cancelPending() {
        for (auto &e: all_)
            if (e.isValid()) e.cancel();
    }
};

template <typename It,
         typename T = typename isFuture<typename std::iterator_traits<It>::value_type>::Inner >
WhenAllFuture<T> whenAll(It begin, It end) {
    return WhenAllFuture<T>(begin, end);
}

}

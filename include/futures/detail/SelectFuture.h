#pragma once

#include <futures/Future.h>
#include <futures/Core.h>

namespace futures {

template <typename Fut>
using SelectFutureItem = std::tuple<Try<typename isFuture<Fut>::Inner>, std::vector<Fut>>;

template <typename Fut>
class SelectFuture : public FutureBase<SelectFuture<Fut>, SelectFutureItem<Fut>> {
public:
    using Item = SelectFutureItem<Fut>;
    using FutureSeq = std::vector<Fut>;
    using value_type = typename isFuture<Fut>::Inner;

    template <typename It>
    SelectFuture(It begin, It end) {
        std::move(begin, end, std::back_inserter(seq_));
    }

    Poll<Item> poll() override {
        if (seq_.size() == 0)
            throw InvalidPollStateException();
        for (size_t i = 0; i < seq_.size(); ++i) {
            auto r = seq_[i].poll();
            if (r.hasException()) {
                remove_nth(i);
                return makePollReady(std::make_tuple(Try<value_type>(r.exception()),
                                std::move(seq_)));
            }
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                remove_nth(i);
                return makePollReady(std::make_tuple(Try<value_type>(std::move(v).value()), std::move(seq_)));
            }
        }
        return Poll<Item>(not_ready);
    }

    void cancel() override {
        for (auto &e: seq_)
            e.cancel();
    }
private:
    FutureSeq seq_;

    void remove_nth(size_t i) {
        if (seq_.size() - 1 != i) {
            std::swap(seq_[i], seq_[seq_.size() - 1]);
            seq_.pop_back();
        }
    }
};

template <typename It,
         typename T = typename std::iterator_traits<It>::value_type,
         typename = typename isFuture<T>::Inner>
SelectFuture<T> makeSelect(It begin, It end) {
    return SelectFuture<T>(begin, end);
}

}

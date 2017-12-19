#pragma once

#include <array>
#include <futures/Future.h>

namespace futures {

namespace detail {

class ICase {
public:
    virtual ~ICase() = default;
    virtual Try<bool> tryMatch() = 0;
};

template <typename Fut, typename F>
class SelectCase : public ICase {
public:
    SelectCase(Fut&& fut, F&& f)
        : fut_(std::move(fut)), func_(std::move(f))
    {}

    Try<bool> tryMatch() override {
        auto r = fut_.poll();
        if (r.hasException())
            return Try<bool>(r.exception());
        if (r->hasValue()) {
            func_(folly::moveFromTry(r).value());
            return Try<bool>(true);
        } else {
            return Try<bool>(false);
        }
    }
private:
    Fut fut_;
    F func_;
};


struct MatchCaseInfo {
    ICase *ptr;
};

template <typename Tuple>
class StaticSelectImpl;

template <class... Ts>
class StaticSelectImpl<std::tuple<Ts...>>
    : public FutureBase<StaticSelectImpl<std::tuple<Ts...>>, size_t>
{
public:
    using tuple_type = std::tuple<Ts...>;
    static constexpr size_t num_cases = sizeof...(Ts);

    using Item = size_t;

    StaticSelectImpl(tuple_type&& tup) : cases_(std::move(tup)) {
        init();
    }

    template <class... Us>
    StaticSelectImpl(Us&&... xs) : cases_(std::forward<Us>(xs)...) {
        init();
    }

    StaticSelectImpl(const StaticSelectImpl&) = delete;
    StaticSelectImpl& operator=(const StaticSelectImpl&) = delete;
    StaticSelectImpl(StaticSelectImpl&& o)
        : cases_(std::move(o.cases_)) {
        init();
    }

    StaticSelectImpl& operator=(StaticSelectImpl&& o) {
        if (&o == this) return *this;
        cases_ = std::move(o);
        init();
        return *this;
    }

    Poll<Item> poll() override {
        for (size_t i = 0; i < arr_.size(); ++i) {
            auto r = arr_[i].ptr->tryMatch();
            if (r.hasException())
                return Poll<Item>(r.exception());
            if (*r)
                return makePollReady(i);
        }
        return Poll<Item>(not_ready);
    }
private:
    void init() {
        std::integral_constant<size_t, 0> first;
        std::integral_constant<size_t, num_cases> last;
        FUTURES_DLOG(INFO) << "select made, size: "
             << sizeof(StaticSelectImpl<tuple_type>);
        init(first, last);
    }

    template <size_t Last>
    void init(std::integral_constant<size_t, Last>,
              std::integral_constant<size_t, Last>) {
    }

    template <size_t First, size_t Last>
    void init(std::integral_constant<size_t, First>,
              std::integral_constant<size_t, Last> last) {
        auto& element = std::get<First>(cases_);
        arr_[First] = MatchCaseInfo{&element};
        init(std::integral_constant<size_t, First + 1>{}, last);
    }

    tuple_type cases_;
    std::array<MatchCaseInfo, num_cases> arr_;
};

template <typename Tuple>
class StaticWhenAllImpl;

template <class... Ts>
class StaticWhenAllImpl<std::tuple<Ts...>>
    : public FutureBase<StaticWhenAllImpl<std::tuple<Ts...>>,
      std::tuple<typename isFuture<Ts>::Inner...>>
{
public:
    using tuple_type = std::tuple<Ts...>;
    using state_type = std::tuple<
            folly::Either<Ts, typename isFuture<Ts>::Inner>...>;
    static constexpr size_t num_cases = sizeof...(Ts);

    static_assert(num_cases > 0, "can not be empty");

    using Item = std::tuple<typename isFuture<Ts>::Inner...>;

    template <class... Us>
    StaticWhenAllImpl(Us&&... xs)
        : cases_(folly::Either<Us, typename isFuture<Us>::Inner>(folly::left_tag, std::forward<Us>(xs))...), finished_(0)
    {
    }

    Poll<Item> poll() override {
        return pollImpl();
    }
private:
    Poll<Item> pollImpl() {
        std::integral_constant<size_t, 0> first;
        std::integral_constant<size_t, num_cases> last;
        FUTURES_DLOG(INFO) << "make whenAll, size: "
             << sizeof(StaticWhenAllImpl<tuple_type>);
        return pollImpl(first, last);
    }

    struct Unwrap {
        template <typename... Us>
        Item operator()(Us&&... xs) {
            return std::make_tuple(std::move(xs.right())...);
        }
    };

    template <size_t Last>
    Poll<Item> pollImpl(std::integral_constant<size_t, Last>,
              std::integral_constant<size_t, Last>) {
        if (finished_ >= num_cases) {
            return makePollReady(folly::applyTuple(Unwrap{}, std::move(cases_)));
        } else {
            return Poll<Item>(not_ready);
        }
    }

    template <size_t First, size_t Last>
    Poll<Item> pollImpl(std::integral_constant<size_t, First>,
              std::integral_constant<size_t, Last> last) {
        auto& element = std::get<First>(cases_);
        if (element.hasLeft()) {
            auto r = element.left().poll();
            if (r.hasException())
                return Poll<Item>(r.exception());
            if (r->isReady()) {
                element.assignRight(std::move(r).value().value());
                finished_++;
            }
        }
        return pollImpl(std::integral_constant<size_t, First + 1>{}, last);
    }

    state_type cases_;
    size_t finished_;
    // std::array<WhenAllCaseInfo, num_cases> arr_;
};


}

template <typename Fut, typename F>
detail::SelectCase<Fut, F> on(Fut&& fut, F&& f) {
    return detail::SelectCase<Fut, F>(std::forward<Fut>(fut), std::forward<F>(f));
}

template <typename... Ts>
detail::StaticSelectImpl<std::tuple<Ts...>> whenAny(Ts&&... xs) {
    using Tuple = std::tuple<Ts...>;
    return detail::StaticSelectImpl<Tuple>(
            std::forward<Ts>(xs)...);
}

template <typename... Ts>
detail::StaticWhenAllImpl<std::tuple<Ts...>> whenAll(Ts&&... xs) {
    using Tuple = std::tuple<Ts...>;
    return detail::StaticWhenAllImpl<Tuple>(
            std::forward<Ts>(xs)...);
}

}

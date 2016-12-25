#pragma once

#include <new>
#include <stdexcept>
#include <type_traits>
#include <iostream>
#include <utility>

namespace folly {

struct LeftTag {};
struct RightTag {};

class EitherEmptyException : public std::runtime_error {
 public:
  EitherEmptyException()
      : std::runtime_error("Empty Either cannot be unwrapped") {}
};

template <typename Left, typename Right>
class Either {
public:
    typedef Left left_type;
    typedef Right right_type;

    static_assert(!std::is_reference<left_type>::value,
            "Either left may not be used with reference types");
    static_assert(!std::is_reference<right_type>::value,
            "Either right may not be used with reference types");
    static_assert(!std::is_abstract<left_type>::value,
            "Either left may not be used with abstract types");
    static_assert(!std::is_abstract<right_type>::value,
            "Either right may not be used with abstract types");

    Either(const LeftTag &l, left_type&& left) {
        construct_left(std::move(left));
    }

    Either(const LeftTag &l, const left_type& left) {
        construct_left(left);
    }

    Either(const RightTag &l, right_type&& right) {
        construct_right(std::move(right));
    }

    Either(const RightTag &l, const right_type& right) {
        construct_right(right);
    }


    ~Either() {
        clear();
    }

    bool hasLeft() const { return state_ == State::LEFT; }
    bool hasRight() const { return state_ == State::RIGHT; }

    left_type &left() & {
        require_left();
        return storage_.left.value;
    }

    left_type left() && {
        require_left();
        return std::move(storage_.left.value);
    }

    const left_type &left() const& {
        require_left();
        return storage_.left.value;
    }

    right_type &right() & {
        require_right();
        return storage_.right.value;
    }

    right_type &right() && {
        require_right();
        return std::move(storage_.right.value);
    }

    const right_type &right() const & {
        require_right();
        return storage_.right.value;
    }

    void assignLeft(left_type &&left) {
        if (state_ == State::LEFT) {
            storage_.left.value = std::move(left);
        } else {
            clear();
            construct_left(std::move(left));
        }
        state_ = State::LEFT;
    }

    void assignRight(right_type &&right) {
        if (state_ == State::RIGHT) {
            storage_.right.value = std::move(right);
        } else {
            clear();
            construct_right(std::move(right));
        }
        state_ = State::RIGHT;
    }

    void assign(Either &&src) {
        if (this != &src) {
            clear();
            if (src.hasLeft()) {
                assignLeft(std::move(src.storage_.left.value));
            } else if (src.hasRight()) {
                assignRight(std::move(src.storage_.right.value));
            }
            src.clear();
        }
    }

    void assignLeft(const left_type &left) {
        if (state_ == State::LEFT) {
            storage_.left.value = left;
        } else {
            clear();
            construct_left(left);
        }
        state_ = State::LEFT;
    }

    void assignRight(const right_type &right) {
        if (state_ == State::RIGHT) {
            storage_.right.value = right;
        } else {
            clear();
            construct_right(right);
        }
        state_ = State::RIGHT;
    }

    void assign(const Either &src) {
        if (this != &src) {
            clear();
           if (src.hasLeft()) {
                assignLeft(std::move(src.storage_.left.value));
            } else if (src.hasRight()) {
                assignRight(std::move(src.storage_.right.value));
            }
        }
    }

    Either& operator=(const Either& src) {
        assign(src);
        return *this;
    }

    Either(const Either& src)
        : state_(State::UNINIT) {
        assign(src);
    }


    Either& operator=(Either&& src) {
        assign(std::move(src));
        return *this;
    }

    Either(Either &&src)
        : state_(State::UNINIT) {
        assign(std::move(src));
    }

private:
    enum class State {
        UNINIT,
        LEFT,
        RIGHT,
    };

    template <typename T>
    struct StorageTriviallyDestructible {
        // uninitialized
        void clear() {
        }

        StorageTriviallyDestructible() {}
        ~StorageTriviallyDestructible() {}

        T value;
    };

    template <typename T>
    struct StorageNonTriviallyDestructible {
        // uninitialized
        void clear() {
            value.~T();
        }
        StorageNonTriviallyDestructible() {}
        ~StorageNonTriviallyDestructible() {}

        T value;
    };

    using StorageLeft =
        typename std::conditional<std::is_trivially_destructible<Left>::value,
                 StorageTriviallyDestructible<Left>,
                 StorageNonTriviallyDestructible<Left>>::type;

    using StorageRight =
        typename std::conditional<std::is_trivially_destructible<Right>::value,
                 StorageTriviallyDestructible<Right>,
                 StorageNonTriviallyDestructible<Right>>::type;

    State state_;

    union storage {
        StorageLeft left;
        StorageRight right;
        storage() {}
        ~storage() {}
    } storage_;

    void clear() {
        if (state_ == State::LEFT) {
            storage_.left.clear();
        } else if (state_ == State::RIGHT) {
            storage_.right.clear();
        }
        state_ = State::UNINIT;
    }

    template<class... Args>
    void construct_left(Args&&... args) {
        const void* ptr = &storage_.left.value;
        // for supporting const types
        new(const_cast<void*>(ptr)) left_type(std::forward<Args>(args)...);
        state_ = State::LEFT;
    }

    template<class... Args>
    void construct_right(Args&&... args) {
        const void* ptr = &storage_.right.value;
        // for supporting const types
        new(const_cast<void*>(ptr)) right_type(std::forward<Args>(args)...);
        state_ = State::RIGHT;
    }


    void require_left() const {
        if (!hasLeft())
            throw EitherEmptyException();
    }

    void require_right() const {
        if (!hasRight())
            throw EitherEmptyException();
    }

};


// Comparisons

template<class L, class R>
bool operator==(const Either<L, R>& a, const Either<L, R>& b) {
    if (a.hasLeft() && b.hasLeft()) {
        return a.left() == b.left();
    } else if (a.hasRight() && b.hasRight()) {
        return a.right() == b.right();
    } else {
        return false;
    }
}

template<class L, class R>
bool operator!=(const Either<L, R>& a, const Either<L, R>& b) {
    return !(a == b);
}

constexpr LeftTag left_tag {};
constexpr RightTag right_tag {};

template<class L, class R, typename T>
Either<L, R> make_left(T&& v) {
    return Either<L, R>(left_tag, std::forward<T>(v));
}

template<class L, class R, typename T>
Either<L, R> make_right(T&& v) {
    return Either<L, R>(right_tag, std::forward<T>(v));
}

}

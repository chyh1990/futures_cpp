#pragma once

#include <iterator>
#include <futures/Core.h>
#include <futures/Exception.h>
#include <futures/Async.h>
#include <futures/Future.h>

namespace futures {

template <typename T>
class IStream {
public:
    virtual Poll<Optional<T>> poll() = 0;
    virtual ~IStream() = default;
};

template <typename T>
struct isStream {
  using Inner = typename folly::Unit::Lift<typename T::Item>::type;
  static const bool value = std::is_base_of<IStream<Inner>, T>::value;
};


template <typename Stream, typename F>
class ForEachFuture;

template <typename Derived, typename T>
class StreamBase : public IStream<T> {
public:
    using Item = T;

    Poll<Optional<T>> poll() override {
        assert(0 && "cannot call base poll");
    }

    template <typename F>
    ForEachFuture<Derived, F> forEach(F&& f);

    StreamBase() = default;
    ~StreamBase() = default;
    StreamBase(StreamBase &&) = default;
    StreamBase& operator=(StreamBase &&) = default;
    StreamBase(const StreamBase &) = delete;
    StreamBase& operator=(const StreamBase &) = delete;
private:
    Derived move_self() {
        return std::move(*static_cast<Derived*>(this));
    }
};

template <typename T>
class EmptyStream : public StreamBase<EmptyStream<T>, T> {
public:
    using Item = T;

    Poll<Optional<T>> poll() override {
        return makePollReady(Optional<T>());
    }
};

template <typename Iter,
         typename T = typename std::iterator_traits<Iter>::value_type>
class IterStream : public StreamBase<IterStream<Iter, T>, T> {
public:
    using Item = T;

    IterStream(Iter begin, Iter end)
        : it_(begin), end_(end) {}

    Poll<Optional<T>> poll() override {
        if (it_ == end_)
            return makePollReady(Optional<T>());
        auto r = folly::make_optional(*it_);
        ++it_;
        return makePollReady(std::move(r));
    }

private:
    Iter it_;
    Iter end_;
};

template <typename Stream>
class StreamSpawn {
public:
    using T = typename isStream<Stream>::Inner;
    typedef Poll<Optional<T>> poll_type;

    poll_type poll_stream(std::shared_ptr<Unpark> unpark) {
        Task task(id_, unpark);

        CurrentTask::WithGuard g(CurrentTask::this_thread(), &task);
        return toplevel_.poll();
    }

    poll_type wait_stream() {
        auto unpark = std::make_shared<ThreadUnpark>();
        while (true) {
            auto r = poll_stream(unpark);
            if (r.hasException())
                return r;
            auto async = folly::moveFromTry(r);
            if (async.isReady()) {
                return poll_type(std::move(async));
            } else {
                unpark->park();
            }
        }
    }

    explicit StreamSpawn(Stream stream)
      : id_(detail::newTaskId()), toplevel_(std::move(stream)) {
    }

    StreamSpawn(StreamSpawn&&) = default;
    StreamSpawn& operator=(StreamSpawn&&) = default;
    StreamSpawn(const StreamSpawn&) = delete;
    StreamSpawn& operator=(const StreamSpawn&) = delete;
private:
    // toplevel future or stream
    unsigned long id_;
    Stream toplevel_;

};

}

#include <futures/Stream-inl.h>

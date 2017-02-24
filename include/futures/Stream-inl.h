#pragma once

#include <futures/Stream.h>

namespace futures {

template <typename Stream, typename F>
class ForEachFuture : public FutureBase<ForEachFuture<Stream, F>, folly::Unit> {
public:
    using Item = folly::Unit;

    ForEachFuture(Stream &&stream, F&& func)
        : stream_(std::move(stream)), func_(std::move(func)) {
    }

    Poll<Item> poll() override {
        while (true) {
            auto r = stream_.poll();
            if (r.hasException())
                return Poll<Item>(r.exception());
            auto v = folly::moveFromTry(r);
            if (v.isReady()) {
                // End of stream
                if (!v->hasValue()) {
                    return makePollReady(folly::Unit());
                } else {
                    try {
                        func_(std::move(v).value().value());
                    } catch (std::exception &e) {
                        return Poll<Item>(folly::exception_wrapper(std::current_exception(), e));
                    }
                }
            } else {
                return Poll<Item>(not_ready);
            }
        }
    }

private:
    Stream stream_;
    F func_;
};

template <typename T, typename F>
class ForEach2Wrapper {
public:
    ForEach2Wrapper(F&& f)
        : f_(std::move(f)) {
    }

    template <typename T0>
    void operator()(T0&& v) {
        return folly::applyTuple(f_, std::forward<T0>(v));
    }
private:
    F f_;
};

template <typename T, typename Stream>
class CollectStreamFuture : public FutureBase<CollectStreamFuture<T, Stream>,
  std::vector<T>>
{
public:
  using Item = std::vector<T>;

  explicit CollectStreamFuture(Stream &&s)
    : stream_(std::move(s)) {}

  Poll<Item> poll() override {
    while (true) {
      auto r = stream_->poll();
      if (r.hasException()) {
          stream_.clear();
          return Poll<Item>(r.exception());
      }
      auto inner = folly::moveFromTry(r);
      if (inner.isReady()) {
        if (inner->hasValue()) {
          vals_.push_back(std::move(inner).value().value());
        } else {
          // EOF
          stream_.clear();
          return makePollReady(std::move(vals_));
        }
      } else {
        return Poll<Item>(not_ready);
      }
    }
  }

private:
  Optional<Stream> stream_;
  std::vector<T> vals_;
};

template <typename T, typename Stream, typename F>
class FilterStream: public StreamBase<FilterStream<T, Stream, F>, T>
{
public:
  using Item = T;
  explicit FilterStream(Stream &&s, F&& func)
    : stream_(std::move(s)), func_(std::move(func)) {}

  Poll<Optional<Item>> poll() override {
    while (true) {
      auto r = stream_->poll();
      if (r.hasException()) {
          stream_.clear();
          return Poll<Optional<Item>>(r.exception());
      }
      auto inner = folly::moveFromTry(r);
      if (inner.isReady()) {
        if (inner->hasValue()) {
          if (func_(inner->value())) {
            return makePollReady(std::move(inner).value());
          }
        } else {
          // EOF
          stream_.clear();
          return makeStreamReady<Item>();
        }
      } else {
        return Poll<Optional<Item>>(not_ready);
      }
    }
  }

private:
  Optional<Stream> stream_;
  F func_;
};

template <typename T, typename Stream, typename F>
class MapStream: public StreamBase<MapStream<T, Stream, F>, T>
{
public:
  using Item = T;
  explicit MapStream(Stream &&s, F&& func)
    : stream_(std::move(s)), func_(std::move(func)) {}

  Poll<Optional<Item>> poll() override {
    while (true) {
      auto r = stream_->poll();
      if (r.hasException()) {
          stream_.clear();
          return Poll<Optional<Item>>(r.exception());
      }
      auto inner = folly::moveFromTry(r);
      if (inner.isReady()) {
        if (inner->hasValue()) {
          try {
            auto v = func_(std::move(inner).value().value());
            return makeStreamReady(std::move(v));
          } catch (std::exception &e) {
            stream_.clear();
            return Poll<Optional<Item>>(
                    folly::exception_wrapper(std::current_exception(), e));
          }
        } else {
          // EOF
          stream_.clear();
          return makeStreamReady<Item>();
        }
      } else {
        return Poll<Optional<Item>>(not_ready);
      }
    }
  }

private:
  Optional<Stream> stream_;
  F func_;
};

template <typename T, typename Stream, typename F>
class AndThenStream: public StreamBase<AndThenStream<T, Stream, F>, T>
{
public:
  using Item = T;
  using StreamT = typename isStream<Stream>::Inner;
  using FutR = typename detail::resultOf<F, StreamT>;

  explicit AndThenStream(Stream &&s, F&& func)
    : stream_(std::move(s)), func_(std::move(func)) {}

  Poll<Optional<Item>> poll() override {
  again:
    if (fut_) {
        auto r = fut_->poll();
        if (r.hasException()) {
            clear();
            return Poll<Optional<Item>>(r.exception());
        }
        auto inner = folly::moveFromTry(r);
        if (inner.hasValue()) {
            fut_.clear();
            return makeStreamReady(std::move(inner).value());
        } else {
            return Poll<Optional<Item>>(not_ready);
        }
    }
    while (true) {
      auto r = stream_->poll();
      if (r.hasException()) {
          stream_.clear();
          return Poll<Optional<Item>>(r.exception());
      }
      auto inner = folly::moveFromTry(r);
      if (inner.isReady()) {
        if (inner->hasValue()) {
            try {
                fut_ = func_(std::move(inner).value().value());
            } catch (std::exception &e) {
                stream_.clear();
                return Poll<Optional<Item>>(
                        folly::exception_wrapper(std::current_exception(), e));
            }
            goto again;
        } else {
          // EOF
          clear();
          return makeStreamReady<Item>();
        }
      } else {
        return Poll<Optional<Item>>(not_ready);
      }
    }
  }
private:
  Optional<Stream> stream_;
  F func_;

  Optional<FutR> fut_;

  void clear() {
      stream_.clear();
      fut_.clear();
  }
};

template <typename T, typename Stream>
class TakeStream : public StreamBase<TakeStream<T, Stream>, T>
{
public:
  using Item = T;

  explicit TakeStream(Stream &&s, size_t remain)
    : stream_(std::move(s)), remain_(remain) {}

  Poll<Optional<Item>> poll() override {
      if (remain_ == 0) {
          stream_.clear();
          return makeStreamReady<Item>();
      }
      auto next = stream_->poll();
      if (next.hasException()) {
          stream_.clear();
          return Poll<Optional<Item>>(next.exception());
      } else if (next->hasValue()) {
          auto v = folly::moveFromTry(next);
          if (v.hasValue()) {
              remain_ --;
              return makePollReady(std::move(v).value());
          } else {
              stream_.clear();
              return makeStreamReady<Item>();
          }
      } else {
          return Poll<Optional<Item>>(not_ready);
      }
  }

private:
  Optional<Stream> stream_;
  size_t remain_;
};

template <typename Stream>
class DropStreamFuture : public FutureBase<DropStreamFuture<Stream>, Unit>
{
public:
  using Item = Unit;

  explicit DropStreamFuture(Stream &&s)
    : stream_(std::move(s)) {}

  Poll<Item> poll() override {
    while (true) {
      auto next = stream_->poll();
      if (next.hasException()) {
          return Poll<Item>(next.exception());
      } else if (next->hasValue()) {
          if (!next->value().hasValue())
              return makePollReady(unit);
      } else {
          return Poll<Item>(not_ready);
      }
    }
  }

private:
  Stream stream_;
};


template <typename Derived, typename T>
BoxedStream<T> StreamBase<Derived, T>::boxed() {
    std::unique_ptr<IStream<T>> p(new Derived(move_self()));
    return BoxedStream<T>(std::move(p));
}

template <typename Derived, typename T>
StreamBase<Derived, T>::operator BoxedStream<T>() && {
    std::unique_ptr<IStream<T>> p(new Derived(move_self()));
    return BoxedStream<T>(std::move(p));
}

template <typename Derived, typename T>
template <typename F>
ForEachFuture<Derived, F> StreamBase<Derived, T>::forEach(F&& f) {
    return ForEachFuture<Derived, F>(move_self(), std::forward<F>(f));
};

template <typename Derived, typename T>
template <typename F>
ForEachFuture<Derived, ForEach2Wrapper<T, F>>
StreamBase<Derived, T>::forEach2(F&& f) {
    return ForEachFuture<Derived, ForEach2Wrapper<T, F>>(move_self(), std::forward<F>(f));
};

template <typename Derived, typename T>
CollectStreamFuture<T, Derived>
StreamBase<Derived, T>::collect() {
    return CollectStreamFuture<T, Derived>(move_self());
}

template <typename Derived, typename T>
template <typename F>
FilterStream<T, Derived, F>
StreamBase<Derived, T>::filter(F&& f) {
    return FilterStream<T, Derived, F>(move_self(), std::forward<F>(f));
}

template <typename Derived, typename T>
template <typename F, typename R>
MapStream<R, Derived, F>
StreamBase<Derived, T>::map(F&& f) {
    return MapStream<R, Derived, F>(move_self(), std::forward<F>(f));
}

template <typename Derived, typename T>
template <typename F, typename FutR, typename R>
AndThenStream<R, Derived, F>
StreamBase<Derived, T>::andThen(F&& f) {
    return AndThenStream<R, Derived, F>(move_self(), std::forward<F>(f));
}

template <typename Derived, typename T>
TakeStream<T, Derived>
StreamBase<Derived, T>::take(size_t n) {
    return TakeStream<T, Derived>(move_self(), n);
}

template <typename Derived, typename T>
DropStreamFuture<Derived>
StreamBase<Derived, T>::drop() {
    return DropStreamFuture<Derived>(move_self());
}

// helper methods
template <typename Iter>
IterStream<Iter> makeIterStream(Iter&& begin, Iter&& end) {
    return IterStream<Iter>(std::forward<Iter>(begin), std::forward<Iter>(end));
}

}

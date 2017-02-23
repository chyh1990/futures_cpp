#pragma once

#include <futures/Stream.h>
# include <boost/iterator/iterator_facade.hpp>

namespace futures {

template <typename Stream>
class StreamIterator
    : public boost::iterator_facade<
      StreamIterator<Stream>,
      typename isStream<Stream>::Inner,
      boost::forward_traversal_tag>
{
public:
    using Item = typename isStream<Stream>::Inner;

    StreamIterator(Stream&& stream)
        : spawn_(StreamSpawn<Stream>(std::move(stream))) {
        loadNext();
    }
    StreamIterator() {}
private:
    friend class boost::iterator_core_access;

    Optional<StreamSpawn<Stream>> spawn_;
    mutable Optional<Item> item_;

    void loadNext() {
        if (!spawn_) return;
        auto r = spawn_->wait_stream();
        r.throwIfFailed();
        item_ = folly::moveFromTry(r).value();
        if (!item_) spawn_.clear();
    }

    void increment() {
        loadNext();
    }

    bool equal(StreamIterator const& other) const
    {
        if (spawn_.hasValue() && other.spawn_.hasValue()) {
            return spawn_->id() == other.spawn_->id();
        } else if (!this->spawn_.hasValue() && !other.spawn_.hasValue()) {
            return true;
        } else {
            return false;
        }
    }

    Item& dereference() const {
        return *item_;
    }

};

template <typename Derived, typename T>
StreamIterator<Derived> StreamBase<Derived, T>::begin() {
    return StreamIterator<Derived>(move_self());
}

template <typename Derived, typename T>
StreamIterator<Derived> StreamBase<Derived, T>::end() {
    return StreamIterator<Derived>();
}

}

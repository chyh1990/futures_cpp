#pragma once

#include <futures/Stream.h>

namespace futures {
namespace channel {

template <typename Recv>
class ReceiverStream
: public StreamBase<ReceiverStream<Recv>, typename Recv::Item> {
public:
    using Item = typename Recv::Item;

    ReceiverStream(Recv&& recv)
        : recv_(std::move(recv)) {}

    Poll<Optional<Item>> poll() override {
        auto r = recv_.poll();
        if (r.template hasException<FutureCancelledException>()) {
            return makePollReady(Optional<Item>());
        } else if (r.hasException()) {
            return Poll<Optional<Item>>(r.exception());
        }
        auto v = folly::moveFromTry(r);
        if (v.isReady()) {
            return makePollReady(Optional<Item>(std::move(v).value()));
        } else {
            return Poll<Optional<Item>>(not_ready);
        }
    }

private:
    Recv recv_;
};

template <typename C>
ReceiverStream<C> makeReceiverStream(C&& c) {
    return ReceiverStream<C>(std::forward<C>(c));
}

}
}

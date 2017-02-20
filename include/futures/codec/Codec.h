#pragma once

#include <futures/Core.h>
#include <futures/Async.h>
#include <futures/Exception.h>
#include <futures/core/IOBufQueue.h>

namespace futures {
namespace codec {

template <class Derived, typename T>
class DecoderBase {
public:
    using Out = T;

    Try<Optional<Out>> decode(folly::IOBufQueue &buf) {
        assert(0 && "unimpl");
    }

    Try<Out> decode_eof(folly::IOBufQueue &buf) {
        auto v = static_cast<Derived*>(this)->decode(buf);
        if (v.hasException())
            return Try<Out>(v.exception());
        if (v->hasValue()) {
            return Try<Out>(folly::moveFromTry(v).value());
        } else {
            return Try<Out>(IOError("eof"));
        }
    }

};

template <class Derived, typename T>
class EncoderBase {
public:
    using Out = T;

    Try<void> encode(Out&& out,
            folly::IOBufQueue &buf) {
        assert(0 && "unimpl");
    }
};


}
}

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

    Optional<Out> decode(folly::IOBufQueue &buf) {
        assert(0 && "unimpl");
    }

    Out decode_eof(folly::IOBufQueue &buf) {
        auto v = static_cast<Derived*>(this)->decode(buf);
        if (v.hasValue()) {
            return std::move(v).value();
        } else {
            throw IOError("decoder eof");
        }
    }

};

template <class Derived, typename T>
class EncoderBase {
public:
    using Out = T;

    void encode(Out&& out,
            folly::IOBufQueue &buf) {
        assert(0 && "unimpl");
    }
};


}
}

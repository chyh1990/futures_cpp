#pragma once

#include <futures/Core.h>
#include <futures/Async.h>
#include <futures/Exception.h>
#include <futures/core/IOBufQueue.h>

namespace futures {
namespace codec {

template <typename T>
class DecoderBase {
public:
    using Out = T;

    virtual Optional<Out> decode(folly::IOBufQueue &buf) = 0;

    virtual Out decodeEof(folly::IOBufQueue &buf) {
        auto v = decode(buf);
        if (v.hasValue()) {
            return std::move(v).value();
        } else {
            throw IOError("decoder eof");
        }
    }

};

template <typename T>
class EncoderBase {
public:
    using Out = T;

    virtual void encode(Out&& out,
            folly::IOBufQueue &buf) = 0;
};


}
}

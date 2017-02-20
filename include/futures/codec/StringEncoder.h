#pragma once

#include <futures/codec/Codec.h>

namespace futures {
namespace codec {

class StringEncoder : public codec::EncoderBase<StringEncoder, std::string> {
public:
    using Out = std::string;

    Try<void> encode(Out&& out, folly::IOBufQueue &buf) {
        auto b = folly::IOBuf::copyBuffer(out.data(), out.length());
        buf.append(std::move(b));
        return Try<void>();

    }
};

}
}

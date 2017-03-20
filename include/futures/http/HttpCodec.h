#pragma once

#include <unordered_map>
#include <futures/io/IoFuture.h>
#include <futures/io/Io.h>
#include <futures/codec/Codec.h>
#include <futures/http/HttpParser.h>

namespace futures {
namespace http {

class HttpV1RequestDecoder :
    public codec::DecoderBase<Request> {
public:
    using Out = Request;

    HttpV1RequestDecoder();

    Optional<Out> decode(folly::IOBufQueue &buf);

private:
    std::unique_ptr<Parser> impl_;
};

class HttpV1ResponseDecoder:
    public codec::DecoderBase<Response> {
public:
    using Out = Response;

    HttpV1ResponseDecoder();

    Optional<Out> decode(folly::IOBufQueue &buf) override;

private:
    std::unique_ptr<Parser> impl_;
};

enum class EncoderLengthMode {
    Unknown,
    ContentLength,
    Chunked,
};

class HttpV1ResponseEncoder:
    public codec::EncoderBase<Response> {
public:
    using Out = Response;

    void encode(Out&& out,
            folly::IOBufQueue &buf) override;

};

class HttpV1RequestEncoder:
    public codec::EncoderBase<Request> {
public:
    using Out = Request;

    HttpV1RequestEncoder(EncoderLengthMode mode = EncoderLengthMode::ContentLength)
        : mode_(mode) {}

    void encode(Out&& out, folly::IOBufQueue &buf) override;

private:
    EncoderLengthMode mode_;
};


}



}

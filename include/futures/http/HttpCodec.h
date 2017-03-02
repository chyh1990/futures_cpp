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

    void encode(Out&& out, folly::IOBufQueue &buf) override;
};


}

namespace websocket {

struct Parser;

class DataFrame {
public:
    enum type_t {
        HANDSHAKE = 0x01,
        HANDSHAKE_RESPONSE = 0x02,
    };

    DataFrame(type_t type)
        : type_(type) {}

    DataFrame(type_t type, http::HttpFrame&& req)
        : type_(type), handshake_(std::move(req)) {}

    type_t getType() const { return type_; }

    const http::HttpFrame* getHandshake() const {
        return handshake_.get_pointer();
    }

    http::HttpFrame* getHandshake() {
        return handshake_.get_pointer();
    }

    const http::HttpFrame* getHandshakeResponse() const {
        return handshake_.get_pointer();
    }

    http::HttpFrame* getHandshakeResponse() {
        return handshake_.get_pointer();
    }

    static DataFrame buildHandshakeResponse(const http::HttpFrame& req);
private:
    type_t type_;
    Optional<http::HttpFrame> handshake_;
};

class RFC6455Decoder : public codec::DecoderBase<DataFrame> {
public:
    using Out = DataFrame;

    RFC6455Decoder();
    ~RFC6455Decoder();

    Optional<Out> decode(folly::IOBufQueue &buf) override;

    RFC6455Decoder(RFC6455Decoder &&);
    RFC6455Decoder& operator=(RFC6455Decoder &&);
private:
    enum State {
        HANDSHAKING,
        STREAMING,
    };

    State s_ = HANDSHAKING;

    std::unique_ptr<http::Parser> handshake_;
    std::unique_ptr<Parser> impl_;
};

class RFC6455Encoder : public codec::EncoderBase<DataFrame> {
public:
    using Out = DataFrame;

    void encode(Out&& out, folly::IOBufQueue &buf) override;

private:
    http::HttpV1ResponseEncoder http_encoder_;
};

}

}

#pragma once

#include <futures/http/HttpCodec.h>
#include <futures/core/Cursor.h>

namespace futures {
namespace websocket {

struct Parser;

class DataFrame {
public:
    enum type_t {
        HANDSHAKE = 0x01,
        HANDSHAKE_RESPONSE = 0x02,
        TEXT = 0x03,
        BINARY = 0x04,
        CLOSE = 0x05,
        PING = 0x06,
        PONG = 0x07,
    };

    DataFrame(type_t type)
        : type_(type) {}

    DataFrame(type_t type, http::HttpFrame&& req)
        : type_(type), handshake_(std::move(req)) {}

    DataFrame(type_t type, const std::string &req)
        : type_(type), data_(req) {}
    DataFrame(type_t type, int status, const std::string &reason)
        : type_(type), data_(reason), status_(status) {}

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

    const std::string &getData() const { return data_; }
private:
    type_t type_;
    Optional<http::HttpFrame> handshake_;
    std::string data_;
    int status_ = 0;
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

    int encodeLength(folly::io::QueueAppender &appender, size_t length);
};

}
}

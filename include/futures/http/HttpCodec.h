#pragma once

#include <unordered_map>
#include <futures/io/IoFuture.h>

namespace futures {
namespace http {

struct Parser;

struct Request {
    unsigned int err;
    unsigned int method;
    uint64_t content_length;
    std::string url;
    std::unordered_map<std::string, std::string> headers;

    folly::IOBufQueue body;

    Request() {
        reset();
    }

    void reset() {
        err = 0;
        method = 0;
        content_length = 0;
        headers.clear();
        url.clear();
        body.clear();
    }

    friend std::ostream& operator<< (std::ostream& stream, const Request& matrix);
};

std::ostream& operator<< (std::ostream& stream, const Request& o);

struct Response {
    unsigned int http_errno;
    std::unordered_map<std::string, std::string> headers;
    folly::IOBufQueue body;

    Response()
      : http_errno(200),
        body(folly::IOBufQueue::cacheChainLength())
    {}
};

class HttpV1Decoder: public io::DecoderBase<HttpV1Decoder, Request> {
public:
    using Out = Request;

    HttpV1Decoder();
    ~HttpV1Decoder();

    Try<Optional<Out>> decode(folly::IOBufQueue &buf);

    HttpV1Decoder(HttpV1Decoder&&);
    HttpV1Decoder& operator=(HttpV1Decoder&&);
private:
    std::unique_ptr<Parser> impl_;
};

class HttpV1Encoder: public io::EncoderBase<HttpV1Encoder, Response> {
public:
    using Out = Response;

    Try<void> encode(Out& out,
            folly::IOBufQueue &buf);

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

    DataFrame(http::Request&& req)
        : type_(HANDSHAKE), handshake_(std::move(req)) {}
    DataFrame(http::Response&& r)
        : type_(HANDSHAKE_RESPONSE), handshake_response_(std::move(r)) {}

    type_t getType() const { return type_; }

    const http::Request* getHandshake() const {
        return handshake_.get_pointer();
    }

    http::Request* getHandshake() {
        return handshake_.get_pointer();
    }

    const http::Response* getHandshakeResponse() const {
        return handshake_response_.get_pointer();
    }

    http::Response* getHandshakeResponse() {
        return handshake_response_.get_pointer();
    }

    static DataFrame buildHandshakeResponse(const http::Request& req);
private:
    type_t type_;
    Optional<http::Request> handshake_;
    Optional<http::Response> handshake_response_;
};

class RFC6455Decoder : public io::DecoderBase<RFC6455Decoder, DataFrame> {
public:
    using Out = DataFrame;

    RFC6455Decoder();
    ~RFC6455Decoder();

    Try<Optional<Out>> decode(folly::IOBufQueue &buf);

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

class RFC6455Encoder : public io::EncoderBase<RFC6455Encoder, DataFrame> {
public:
    using Out = DataFrame;

    Try<void> encode(Out& out,
            folly::IOBufQueue &buf);

private:
    http::HttpV1Encoder http_encoder_;
};

}

}

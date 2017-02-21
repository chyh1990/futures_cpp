#pragma once

#include <unordered_map>
#include <futures/io/IoFuture.h>
#include <futures/io/Io.h>
#include <futures/codec/Codec.h>

namespace futures {
namespace http {

struct Parser;

struct HttpFrame {
    unsigned int err;
    unsigned int http_errno;
    unsigned int method;
    uint64_t content_length;
    std::string path;
    std::unordered_map<std::string, std::string> headers;

    folly::IOBufQueue body;

    HttpFrame()
        : body(folly::IOBufQueue::cacheChainLength()) {
        reset();
    }

    void reset() {
        err = 0;
        method = 0;
        content_length = 0;
        http_errno = 0;
        headers.clear();
        path.clear();
        body.clear();
    }

    friend std::ostream& operator<< (std::ostream& stream, const HttpFrame& frame);
};

class Request : public HttpFrame {
public:
    Request(HttpFrame &&f)
        : HttpFrame(std::move(f)) {}

    Request() {}
};

class Response : public HttpFrame {
public:
    Response() {}
    Response(HttpFrame &&f)
        : HttpFrame(std::move(f)) {}
};

std::ostream& operator<< (std::ostream& stream, const HttpFrame& o);

class HttpV1RequestDecoder: public codec::DecoderBase<HttpV1RequestDecoder, Request> {
public:
    using Out = Request;

    HttpV1RequestDecoder();
    ~HttpV1RequestDecoder();

    Try<Optional<Out>> decode(folly::IOBufQueue &buf);

    HttpV1RequestDecoder(HttpV1RequestDecoder&&);
    HttpV1RequestDecoder& operator=(HttpV1RequestDecoder&&);
private:
    std::unique_ptr<Parser> impl_;
};

class HttpV1ResponseDecoder:
    public codec::DecoderBase<HttpV1ResponseDecoder, Response> {
public:
    using Out = Response;

    HttpV1ResponseDecoder();
    ~HttpV1ResponseDecoder();

    Try<Optional<Out>> decode(folly::IOBufQueue &buf);

    HttpV1ResponseDecoder(HttpV1ResponseDecoder&&);
    HttpV1ResponseDecoder& operator=(HttpV1ResponseDecoder&&);
private:
    std::unique_ptr<Parser> impl_;
};


class HttpV1ResponseEncoder:
    public codec::EncoderBase<HttpV1ResponseEncoder, Response> {
public:
    using Out = Response;

    Try<void> encode(Out&& out,
            folly::IOBufQueue &buf);

};

class HttpV1RequestEncoder:
    public codec::EncoderBase<HttpV1RequestEncoder, Request> {
public:
    using Out = Request;

    Try<void> encode(Out&& out,
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

class RFC6455Decoder : public codec::DecoderBase<RFC6455Decoder, DataFrame> {
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

class RFC6455Encoder : public codec::EncoderBase<RFC6455Encoder, DataFrame> {
public:
    using Out = DataFrame;

    Try<void> encode(Out&& out,
            folly::IOBufQueue &buf);

private:
    http::HttpV1ResponseEncoder http_encoder_;
};

}

}

#pragma once

#include <vector>
#include <unordered_map>
#include <futures/core/IOBufQueue.h>
#include <futures/http/http_parser.h>
struct http_parser;

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
    bool eof_;

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
        eof_ = false;
    }

    bool hasContentLength() const {
        return content_length < INT64_MAX;
    }

    friend std::ostream& operator<< (std::ostream& stream, const HttpFrame& frame);

private:
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



struct Parser {
    enum State {
        INIT,
        HEADER,
        VALUE,
    };

    static int url_cb(http_parser* parser, const char *at, size_t length);
    static int header_field_cb(http_parser* parser, const char *at, size_t length);

    static int header_value_cb(http_parser* parser, const char *at, size_t length);

    static int body_cb(http_parser* parser, const char *at, size_t length);

    static int header_complete_cb(http_parser *parser);
    static int message_begin_cb(http_parser* parser);

    static int message_complete_cb(http_parser* parser);

    Parser(bool is_request, bool allow_body = true);

    HttpFrame moveResult() {
        // websocket handshake not completed
        // assert(completed_);
        if (completed_) {
            req_.eof_ = true;
            header_completed_ = false;
            completed_ = false;
        }
        HttpFrame newf;
        std::swap(newf, req_);
        return newf;
    }

    const HttpFrame &getResult() const {
        return req_;
    }

    size_t execute(const folly::IOBuf* buf);

    bool hasCompeleted() const {
        return completed_;
    }

    bool hasHeaderCompeleted() const {
        return header_completed_;
    }

    const http_parser &getParser() const {
        return parser_;
    }

private:
    bool allow_body_;
    std::string field_;
    std::string value_;
    State s_;

    http_parser parser_;
    http_parser_settings settings_;
    bool header_completed_;
    bool completed_;

    const folly::IOBuf *cur_buf_ = nullptr;
    HttpFrame req_;

    void reset() {
        header_completed_ = false;
        completed_ = false;
        field_.clear();
        value_.clear();
        req_.reset();
        s_ = INIT;
    }
};


}
}

#include <futures/io/StreamAdapter.h>
#include <futures/http/HttpCodec.h>
#include <futures/http/http_parser.h>
extern "C" {
#include <futures/http/ws_parser.h>
}
#include <unordered_map>

#include "cryptlite/base64.h"
#include "cryptlite/sha1.h"

namespace futures {
namespace http {

std::ostream& operator<< (std::ostream& stream, const Request& o) {
    stream << "HTTP: " << http_method_str((http_method)o.method)
        << " " << (int)o.err << "[" << http_errno_name((http_errno)o.err) << "]\n";
    stream << "Content-Length: " << o.content_length << "\n";
    stream << "Headers: \n";
    for (auto &e: o.headers) {
        stream << "  " << e.first << ": " << e.second << "\n";
    }
    stream << std::endl;
    return stream;
}

struct Parser {
    enum State {
        INIT,
        HEADER,
        VALUE,
    };

    static int url_cb(http_parser* parser, const char *at, size_t length) {
        Parser *self = static_cast<Parser*>(parser->data);
        self->req_.url.append(std::string(at, length));

        return 0;
    }

    static int header_field_cb(http_parser* parser, const char *at, size_t length) {
        Parser *self = static_cast<Parser*>(parser->data);
        if (self->s_ == VALUE)
            self->req_.headers[self->field_] = self->value_;
        if (self->s_ == INIT || self->s_ == VALUE) {
            self->field_.assign(at, length);
        } else if (self->s_ == HEADER) {
            self->field_.append(std::string(at, length));
        } else {
            assert(0);
        }
        self->s_ = HEADER;
        return 0;
    }

    static int header_value_cb(http_parser* parser, const char *at, size_t length) {
        Parser *self = static_cast<Parser*>(parser->data);
        if (self->s_ == VALUE) {
            self->value_.append(std::string(at, length));
        } else if (self->s_ == HEADER) {
            self->value_.assign(at, length);
        } else {
            assert(0);
        }
        self->s_ = VALUE;
        return 0;
    }

    static int body_cb(http_parser* parser, const char *at, size_t length) {
        // FUTURES_DLOG(INFO) << "BODY: " << at << ", " << length;
        // if (!length) return 0;
        assert(length > 0);
        Parser *self = static_cast<Parser*>(parser->data);
        if (!self->allow_body_)
            return 1;
        assert(self->cur_buf_);
        assert(self->cur_buf_->data() <= (const uint8_t*)at);
        assert(self->cur_buf_->tail() >= (const uint8_t*)(at + length));

        auto buf = self->cur_buf_->clone();
        buf->trimStart((const uint8_t*)at - self->cur_buf_->data());
        self->req_.body.append(std::move(buf));
        return 0;
    }

    static int header_complete_cb(http_parser *parser) {
        Parser *self = static_cast<Parser*>(parser->data);
        if (self->s_ == VALUE)
            self->req_.headers[self->field_] = self->value_;
        self->req_.err = parser->http_errno;
        self->req_.method = parser->method;
        self->req_.content_length = parser->content_length;
        return 0;
    }

    static int message_begin_cb(http_parser* parser) {
        Parser *self = static_cast<Parser*>(parser->data);
        if (self->completed_) {
            FUTURES_LOG(WARNING) << "message begin without consuming previous result";
            return 1;
        }
        self->reset();
        return 0;
    }

    static int message_complete_cb(http_parser* parser) {
        Parser *self = static_cast<Parser*>(parser->data);
        FUTURES_DLOG(INFO) << "completed ";
        self->completed_ = true;
        return 0;
    }

    Parser(bool allow_body = true) : allow_body_(allow_body) {
        http_parser_settings_init(&settings_);
        settings_.on_message_begin = Parser::message_begin_cb;
        settings_.on_url = Parser::url_cb;
        settings_.on_header_field = Parser::header_field_cb;
        settings_.on_header_value = Parser::header_value_cb;
        settings_.on_headers_complete = Parser::header_complete_cb;
        settings_.on_body = Parser::body_cb;
        settings_.on_message_complete = Parser::message_complete_cb;
        http_parser_init(&parser_, HTTP_REQUEST);
        parser_.data = this;

        reset();
    }

    Request moveRequest() {
        // websocket handshake not completed
        // assert(completed_);
        completed_ = false;
        return std::move(req_);
    }

    const Request &getRequest() const {
        return req_;
    }

    size_t execute(const std::unique_ptr<folly::IOBuf>& buf) {
        assert(!cur_buf_);
        cur_buf_ = buf.get();
        size_t nparsed = http_parser_execute(&parser_, &settings_,
                (const char*)buf->data(), buf->length());
        cur_buf_ = nullptr;
        return nparsed;
    }

    bool hasCompeleted() const {
        return completed_;
    }

    const http_parser &getParser() const {
        return parser_;
    }

private:
    const bool allow_body_;
    std::string field_;
    std::string value_;
    State s_;

    http_parser parser_;
    http_parser_settings settings_;
    bool completed_;

    const folly::IOBuf *cur_buf_ = nullptr;
    Request req_;

    void reset() {
        completed_ = false;
        field_.clear();
        value_.clear();
        req_.reset();
        s_ = INIT;
    }
};

HttpV1Decoder::HttpV1Decoder()
    : impl_(new Parser()) {
}

HttpV1Decoder::~HttpV1Decoder() = default;
HttpV1Decoder::HttpV1Decoder(HttpV1Decoder&&) = default;
HttpV1Decoder& HttpV1Decoder::operator=(HttpV1Decoder&&) = default;

Try<Optional<HttpV1Decoder::Out>> HttpV1Decoder::decode(folly::IOBufQueue &buf)
{
    auto front = buf.pop_front();
    assert(front);
    assert(front->countChainElements() == 1);
    size_t nparsed = impl_->execute(std::move(front));
    if (impl_->getParser().upgrade) {
        return Try<Optional<Out>>(IOError("unsupported"));
    } else if (nparsed != front->length()) {
        return Try<Optional<Out>>(IOError("invalid http request"));
    }
    if (!front->isShared()) {
        front->clear();
        buf.append(std::move(front));
    }
    if (impl_->hasCompeleted()) {
        return Try<Optional<Out>>(Optional<Out>(impl_->moveRequest()));
    } else {
        return Try<Optional<Out>>(Optional<Out>());
    }
}

static const size_t kMaxHttpStatusNumber = 511;

struct ErrorStatusStrings {
    ErrorStatusStrings() {
#define XX(num, name, string) status_lines[num] = #string ;
        HTTP_STATUS_MAP(XX)
#undef XX
    }

    const char *get(unsigned int http_errno) {
        if (http_errno < 100 || http_errno > kMaxHttpStatusNumber)
            return nullptr;
        return status_lines[http_errno];
    }
private:
    const char *status_lines[kMaxHttpStatusNumber + 1];
};

static const char *getHttpStatusLine(unsigned int http_errno) {
    static ErrorStatusStrings errs;
    return errs.get(http_errno);
}

Try<void> HttpV1Encoder::encode(http::Response& out,
        folly::IOBufQueue &buf) {
    IOBufStreambuf sb(&buf);
    std::ostream ss(&sb);
    const char *error_line = getHttpStatusLine(out.http_errno);
    if (!error_line) return Try<void>(IOError("invalid http response code"));

    // std::ostringstream ss;
    ss << "HTTP/1.1 " << out.http_errno << ' ' << error_line << "\r\n";
    for (auto &e: out.headers)
        ss << e.first << ": " << e.second << "\r\n";
    if (!out.body.empty()) {
        ss << "Content-Length: " << out.body.chainLength() << "\r\n";
    }
    auto it = out.headers.find("Connection");
    if (it == out.headers.end())
        ss << "Connection: keep-alive\r\n";
    ss << "\r\n";
    ss.flush();
    buf.append(std::move(out.body), false);

    return Try<void>();
}

}

namespace websocket {

struct Parser {
public:
    static int data_begin_cb(void *self, ws_frame_type_t type) {
        FUTURES_DLOG(INFO) << "data: " << type;
        return 0;
    }

    static int data_payload_cb(void *self, const char* at, size_t len) {
        FUTURES_DLOG(INFO) << "data: " << at << ", " << len;
        return 0;
    }

    static int data_end_cb(void *self) {
        return 0;
    }

    static int control_begin_cb(void *self, ws_frame_type_t type) {
        FUTURES_DLOG(INFO) << "control: " << type;
        return 0;
    }

    static int control_payload_cb(void *self, const char* at, size_t len) {
        FUTURES_DLOG(INFO) << "control: " << at << ", " << len;
        return 0;
    }

    static int control_end_cb(void *self) {
        return 0;
    }

    Parser() {
        cbs_.on_data_begin = Parser::data_begin_cb;
        cbs_.on_data_payload = Parser::data_payload_cb;
        cbs_.on_data_end = Parser::data_end_cb;
        cbs_.on_control_begin = Parser::control_begin_cb;
        cbs_.on_control_payload = Parser::control_payload_cb;
        cbs_.on_control_end = Parser::control_end_cb;
        ws_parser_init(&parser_, &cbs_);
        parser_.user_data = this;
    }

    int execute(std::unique_ptr<folly::IOBuf> &buf) {
        int nparsed = ws_parser_execute(&parser_, (char*)buf->data(), buf->length());
        FUTURES_DLOG(INFO) << "NPARSED: " << nparsed;
        return nparsed;
    }

private:
    ws_parser_t parser_;
    ws_parser_callbacks_t cbs_;
};

RFC6455Decoder::RFC6455Decoder()
    : handshake_(new http::Parser(false)),
      impl_(new Parser()) {
}

inline bool upgradeToWebsocket(const http::Request& req) {
    auto it = req.headers.find("Upgrade");
    if (it == req.headers.end() || it->second != "websocket")
        return false;
    it = req.headers.find("Sec-WebSocket-Version");
    if (it == req.headers.end() || it->second != "13")
        return false;
    return true;
}

Try<Optional<DataFrame>>
RFC6455Decoder::decode(folly::IOBufQueue &buf) {
    auto front = buf.pop_front();
    assert(front);
    assert(front->countChainElements() == 1);
    if (s_ == HANDSHAKING) {
        size_t nparsed = handshake_->execute(std::move(front));
        if (handshake_->getParser().upgrade) {
            if (!upgradeToWebsocket(handshake_->getRequest()))
                return Try<Optional<Out>>(IOError("unsupported"));
            FUTURES_DLOG(INFO) << "Upgrading";
            s_ = STREAMING;
            return Try<Optional<Out>>(DataFrame(handshake_->moveRequest()));
        } else if (nparsed != front->length()) {
            return Try<Optional<Out>>(IOError("invalid http request"));
        } else {
            return Try<Optional<Out>>(Optional<Out>());
        }
    } else {
        int nparsed = impl_->execute(front);
        return Try<Optional<Out>>(Optional<Out>());
    }
}

static inline std::string acceptKey(const std::string &str) {
    static const char* kGUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    cryptlite::sha1 s;
    s.input((const uint8_t*)str.data(), str.size());
    s.input((const uint8_t*)kGUID, 36);
    uint8_t digest[cryptlite::sha1::HASH_SIZE];
    s.result(digest);

    return cryptlite::base64::encode_from_array(digest, cryptlite::sha1::HASH_SIZE);
}

DataFrame DataFrame::buildHandshakeResponse(const http::Request& req)
{
    const size_t kMaxTokenSize = 128;
    http::Response resp;
    resp.http_errno = 101;
    resp.headers["Upgrade"] = "websocket";
    resp.headers["Connection"] = "Upgrade";
    auto it = req.headers.find("Sec-WebSocket-Key");
    if (it == req.headers.end() || it->second.size() > kMaxTokenSize)
        throw std::invalid_argument("invalid Sec-WebSocket-Key");

    resp.headers["Sec-WebSocket-Accept"] = acceptKey(it->second);

    return DataFrame(std::move(resp));
}

RFC6455Decoder::~RFC6455Decoder() = default;
RFC6455Decoder::RFC6455Decoder(RFC6455Decoder &&) = default;
RFC6455Decoder& RFC6455Decoder::operator=(RFC6455Decoder &&) = default;

Try<void> RFC6455Encoder::encode(DataFrame& out,
        folly::IOBufQueue &buf) {
    if (out.getType() == DataFrame::HANDSHAKE_RESPONSE) {
        return http_encoder_.encode(*out.getHandshakeResponse(), buf);
    } else {
        return Try<void>(IOError("unimpl"));
    }
}

}

}

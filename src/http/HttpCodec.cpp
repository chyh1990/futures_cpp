#include <futures/http/HttpCodec.h>
#include <futures/http/http_parser.h>
#include <unordered_map>

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

    http_parser parser_;
    http_parser_settings settings_;

    bool completed_;

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
        self->reset();
        return 0;
    }

    static int message_complete_cb(http_parser* parser) {
        Parser *self = static_cast<Parser*>(parser->data);
        FUTURES_DLOG(INFO) << "completed ";
        self->completed_ = true;
        return 0;
    }

    Parser() {
        http_parser_settings_init(&settings_);
        settings_.on_url = Parser::url_cb;
        settings_.on_message_begin = Parser::message_begin_cb;
        settings_.on_message_complete = Parser::message_complete_cb;
        settings_.on_header_field = Parser::header_field_cb;
        settings_.on_header_value = Parser::header_value_cb;
        settings_.on_headers_complete = Parser::header_complete_cb;
        http_parser_init(&parser_, HTTP_REQUEST);
        parser_.data = this;

        reset();
    }

    Request moveRequest() {
        assert(completed_);
        completed_ = false;
        return std::move(req_);
    }

private:
    std::string field_;
    std::string value_;
    State s_;

    Request req_;

    void reset() {
        completed_ = false;
        field_.clear();
        value_.clear();
        req_.reset();
        s_ = INIT;
    }
};

HttpV1Codec::HttpV1Codec()
    : impl_(new Parser()) {
}

HttpV1Codec::~HttpV1Codec() = default;
HttpV1Codec::HttpV1Codec(HttpV1Codec&&) = default;
HttpV1Codec& HttpV1Codec::operator=(HttpV1Codec&&) = default;

Try<Optional<HttpV1Codec::In>> HttpV1Codec::decode(std::unique_ptr<folly::IOBuf> &buf)
{
     size_t nparsed = http_parser_execute(&impl_->parser_, &impl_->settings_,
             (const char*)buf->data(), buf->length());
     if (impl_->parser_.upgrade) {
         return Try<Optional<In>>(IOError("unsupported"));
     } else if (nparsed != buf->length()) {
         return Try<Optional<In>>(IOError("invalid http request"));
     }
     buf->trimStart(nparsed);
     if (impl_->completed_) {
         return Try<Optional<In>>(Optional<In>(impl_->moveRequest()));
     } else {
         return Try<Optional<In>>(Optional<In>());
     }
}

Try<folly::Unit> HttpV1Codec::encode(const http::Response& out,
        std::unique_ptr<folly::IOBuf> &buf) {
#if 1
    std::stringstream ss;
    ss << "HTTP/1.1 200 OK" << "\r\n";
    for (auto &e: out.headers)
        ss << e.first << ": " << e.second << "\r\n";
    if (out.body.size()) {
        ss << "Content-Length: " << out.body.size() << "\r\n";
    }
    ss << "Connection: keep-alive\r\n\r\n";
    if (out.body.size())
        ss << out.body;
    auto s = ss.str();
    FUTURES_DLOG(INFO) << "OUT: " << s;
    size_t len = s.size();
    buf->reserve(0, len);
    memcpy(buf->writableTail(), s.data(), len);
    buf->append(len);
#else
    static const char *kResponse = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHELLO";
    size_t len = strlen(kResponse);
    buf->reserve(0, len);
    memcpy(buf->writableTail(), kResponse, len);
    buf->append(len);
#endif

    return Try<folly::Unit>(folly::Unit());
}

#if 0
void HttpV1Handler::read(HttpV1Handler::Context* ctx, folly::IOBufQueue &msg) {
    while (!msg.empty()) {
        auto front = msg.pop_front();

        size_t nparsed = http_parser_execute(&impl_->parser_, &impl_->settings_,
                (const char*)front->data(), front->length());
        if (impl_->parser_.upgrade) {
            throw IOError("unsupported");
        } else if (nparsed != front->length()) {
            throw IOError("invalid http request");
        }
        if (impl_->completed_) {
            FUTURES_DLOG(INFO) << "new req: ";
            ctx->fireRead(impl_->moveRequest());
        }
    }
}

HttpV1Handler::HttpV1Handler()
    : impl_(new Parser()) {
    }
HttpV1Handler::~HttpV1Handler() = default;
#endif

}
}

#include <futures/Core.h>
#include <futures/http/HttpParser.h>

namespace futures {
namespace http {

std::ostream& operator<< (std::ostream& stream, const HttpFrame& o) {
    stream << "HTTP: " << http_method_str((http_method)o.method)
        << " " << (int)o.err << "[" << http_errno_name((http_errno)o.err) << "]\n";
    stream << "Path: " << o.path << "\n";
    stream << "Content-Length: " << o.content_length << "\n";
    stream << "Body-Size: " << o.body.chainLength() << "\n";
    stream << "Headers: \n";
    for (auto &e: o.headers) {
        stream << "  " << e.first << ": " << e.second << "\n";
    }
    stream << std::endl;
    return stream;
}

int Parser::url_cb(http_parser* parser, const char *at, size_t length) {
    Parser *self = static_cast<Parser*>(parser->data);
    self->req_.path.append(std::string(at, length));

    return 0;
}

int Parser::header_field_cb(http_parser* parser, const char *at, size_t length) {
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

int Parser::header_value_cb(http_parser* parser, const char *at, size_t length) {
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

int Parser::body_cb(http_parser* parser, const char *at, size_t length) {
    // FUTURES_DLOG(INFO) << "BODY: " << at << ", " << length;
    // if (!length) return 0;
    assert(length > 0);
    Parser *self = static_cast<Parser*>(parser->data);
    assert(self->cur_buf_);
    assert(self->cur_buf_->data() <= (const uint8_t*)at);
    assert(self->cur_buf_->tail() >= (const uint8_t*)(at + length));

    auto buf = self->cur_buf_->clone();
    size_t trim_start = (const uint8_t*)at - self->cur_buf_->data();
    size_t trim_end = self->cur_buf_->tail() - (const uint8_t*)(at + length);
    buf->trimStart(trim_start);
    buf->trimEnd(trim_end);
    self->req_.body.append(std::move(buf));
    return 0;
}

int Parser::header_complete_cb(http_parser *parser) {
    Parser *self = static_cast<Parser*>(parser->data);
    if (self->s_ == VALUE)
        self->req_.headers[self->field_] = self->value_;
    self->req_.err = parser->http_errno;
    self->req_.method = parser->method;
    self->req_.content_length = parser->content_length;
    self->header_completed_ = true;
    if (!self->allow_body_) {
        return 1;
    }
    return 0;
}

int Parser::message_begin_cb(http_parser* parser) {
    Parser *self = static_cast<Parser*>(parser->data);
    if (self->completed_) {
        FUTURES_LOG(WARNING) << "message begin without consuming previous result";
        return 1;
    }
    self->reset();
    return 0;
}


int Parser::message_complete_cb(http_parser* parser) {
    Parser *self = static_cast<Parser*>(parser->data);
    FUTURES_DLOG(INFO) << "completed ";
    self->completed_ = true;
    return 0;
}

Parser::Parser(bool is_request, bool allow_body)
    : allow_body_(allow_body) {
    http_parser_settings_init(&settings_);
    settings_.on_message_begin = Parser::message_begin_cb;
    settings_.on_url = Parser::url_cb;
    settings_.on_header_field = Parser::header_field_cb;
    settings_.on_header_value = Parser::header_value_cb;
    settings_.on_headers_complete = Parser::header_complete_cb;
    settings_.on_body = Parser::body_cb;
    settings_.on_message_complete = Parser::message_complete_cb;
    http_parser_init(&parser_, is_request ? HTTP_REQUEST : HTTP_RESPONSE);
    parser_.data = this;

    reset();
}

size_t Parser::execute(const folly::IOBuf* buf) {
    assert(!cur_buf_);
    cur_buf_ = buf;
    size_t nparsed = http_parser_execute(&parser_, &settings_,
            (const char*)buf->data(), buf->length());
    cur_buf_ = nullptr;
    return nparsed;
}



}
}

#include <futures/io/StreamAdapter.h>
#include <futures/http/HttpCodec.h>
extern "C" {
#include <futures/http/ws_parser.h>
}
#include <unordered_map>

#include "cryptlite/base64.h"
#include "cryptlite/sha1.h"

namespace futures {
namespace http {

HttpV1RequestDecoder::HttpV1RequestDecoder()
    : impl_(new Parser(true)) {
}

Optional<HttpV1RequestDecoder::Out>
HttpV1RequestDecoder::decode(folly::IOBufQueue &buf)
{
    while (!buf.empty()) {
        auto front = buf.pop_front();
        size_t nparsed = impl_->execute(front.get());
        if (impl_->getParser().upgrade) {
            throw IOError("upgrade unsupported");
        } else if (nparsed != front->length()) {
            throw IOError("invalid http request");
        }
    }
    if (impl_->hasCompeleted()) {
        return Optional<Out>(impl_->moveResult());
    } else {
        return none;
    }
}

HttpV1ResponseDecoder::HttpV1ResponseDecoder()
    : impl_(new Parser(false)) {
    }

Optional<HttpV1ResponseDecoder::Out>
HttpV1ResponseDecoder::decode(folly::IOBufQueue &buf)
{
    while (!buf.empty()) {
        auto front = buf.pop_front();
        size_t nparsed = impl_->execute(front.get());
        if (nparsed != front->length())
            throw IOError("invalid http response");
    }
    if (impl_->hasCompeleted()) {
        return Optional<Out>(impl_->moveResult());
    } else {
        return none;
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

void HttpV1ResponseEncoder::encode(
        http::Response&& out,
        folly::IOBufQueue &buf) {
    IOBufStreambuf sb(&buf);
    std::ostream ss(&sb);
    const char *error_line = getHttpStatusLine(out.http_errno);
    if (!error_line) throw IOError("invalid http response code");

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

}

void HttpV1RequestEncoder::encode(
        http::Request&& out,
        folly::IOBufQueue &buf) {
    IOBufStreambuf sb(&buf);
    std::ostream ss(&sb);

    // std::ostringstream ss;
    ss << http_method_str((http_method)out.method) <<
        ' ' << out.path << " HTTP/1.1\r\n";
    for (auto &e: out.headers)
        ss << e.first << ": " << e.second << "\r\n";
    if (!out.body.empty()) {
        ss << "Content-Length: " << out.body.chainLength() << "\r\n";
    }
    ss << "\r\n";
    ss.flush();
    buf.append(std::move(out.body), false);

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

inline bool upgradeToWebsocket(const http::HttpFrame& req) {
    auto it = req.headers.find("Upgrade");
    if (it == req.headers.end() || it->second != "websocket")
        return false;
    it = req.headers.find("Sec-WebSocket-Version");
    if (it == req.headers.end() || it->second != "13")
        return false;
    return true;
}

Optional<DataFrame>
RFC6455Decoder::decode(folly::IOBufQueue &buf) {
    auto front = buf.pop_front();
    assert(front);
    assert(front->countChainElements() == 1);
    if (s_ == HANDSHAKING) {
        size_t nparsed = handshake_->execute(front.get());
        if (handshake_->getParser().upgrade) {
            if (!upgradeToWebsocket(handshake_->getResult()))
                throw IOError("unsupported");
            FUTURES_DLOG(INFO) << "Upgrading";
            s_ = STREAMING;
            return DataFrame(DataFrame::HANDSHAKE, handshake_->moveResult());
        } else if (nparsed != front->length()) {
            throw IOError("invalid http request");
        } else {
            return none;
        }
    } else {
        int nparsed = impl_->execute(front);
        return none;
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

DataFrame DataFrame::buildHandshakeResponse(const http::HttpFrame& req)
{
    const size_t kMaxTokenSize = 128;
    http::HttpFrame resp;
    resp.http_errno = 101;
    resp.headers["Upgrade"] = "websocket";
    resp.headers["Connection"] = "Upgrade";
    auto it = req.headers.find("Sec-WebSocket-Key");
    if (it == req.headers.end() || it->second.size() > kMaxTokenSize)
        throw std::invalid_argument("invalid Sec-WebSocket-Key");

    resp.headers["Sec-WebSocket-Accept"] = acceptKey(it->second);

    return DataFrame(DataFrame::HANDSHAKE_RESPONSE, std::move(resp));
}

RFC6455Decoder::~RFC6455Decoder() = default;
RFC6455Decoder::RFC6455Decoder(RFC6455Decoder &&) = default;
RFC6455Decoder& RFC6455Decoder::operator=(RFC6455Decoder &&) = default;

void RFC6455Encoder::encode(DataFrame&& out,
        folly::IOBufQueue &buf) {
    if (out.getType() == DataFrame::HANDSHAKE_RESPONSE) {
        return http_encoder_.encode(std::move(*out.getHandshakeResponse()), buf);
    } else {
        throw IOError("unimpl");
    }
}

}

}

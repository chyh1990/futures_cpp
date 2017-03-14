#include <futures/http/WsCodec.h>
extern "C" {
#include <futures/http/ws_parser.h>
}

#include <deque>
#include <futures/core/Cursor.h>
#include "cryptlite/base64.h"
#include "cryptlite/sha1.h"

namespace futures {
namespace websocket {

struct Parser {
public:
    using queue_type = std::deque<std::pair<ws_frame_type_t, std::string>>;

    static int data_begin_cb(void *self, ws_frame_type_t type) {
        // FUTURES_DLOG(INFO) << "data: " << type;
        Parser *parser = static_cast<Parser*>(self);
        parser->cur_data_.clear();
        parser->cur_data_type_ = type;
        return 0;
    }

    static int data_payload_cb(void *self, const char* at, size_t len) {
        // FUTURES_DLOG(INFO) << "data: " << at << ", " << len;
        Parser *parser = static_cast<Parser*>(self);
        if (parser->cur_data_.length() + len > 1024 * 1024)
            return WS_CONTROL_TOO_LONG;
        parser->cur_data_ += std::string(at, len);
        return 0;
    }

    static int data_end_cb(void *self) {
        Parser *parser = static_cast<Parser*>(self);
        parser->q_.push_back(std::make_pair(parser->cur_data_type_, parser->cur_data_));
        return 0;
    }

    static int control_begin_cb(void *self, ws_frame_type_t type) {
        // FUTURES_DLOG(INFO) << "control: " << type;
        Parser *parser = static_cast<Parser*>(self);
        parser->cur_control_.clear();
        parser->cur_control_type_ = type;
        return 0;
    }

    static int control_payload_cb(void *self, const char* at, size_t len) {
        // FUTURES_DLOG(INFO) << "control: " << at << ", " << len;
        Parser *parser = static_cast<Parser*>(self);
        parser->cur_control_ += std::string(at, len);
        return 0;
    }

    static int control_end_cb(void *self) {
        Parser *parser = static_cast<Parser*>(self);
        parser->q_.push_back(std::make_pair(parser->cur_control_type_, parser->cur_control_));
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
        return nparsed;
    }

    bool hasData() const { return !q_.empty(); }

    Optional<DataFrame> pollFrame() {
        if (!hasData()) return none;
        auto v = std::move(q_.front());
        q_.pop_front();
        DataFrame::type_t type;
        switch (v.first) {
            case WS_FRAME_TEXT:
                type = DataFrame::TEXT;
                break;
            case WS_FRAME_BINARY:
                type = DataFrame::BINARY;
                break;
            case WS_FRAME_CLOSE:
                type = DataFrame::CLOSE;
                break;
            case WS_FRAME_PING:
                type = DataFrame::PING;
                break;
            case WS_FRAME_PONG:
                type = DataFrame::PONG;
                break;
            default:
                throw std::runtime_error("Invalid frame type");
        }
        return folly::make_optional(DataFrame(type, std::move(v.second)));
    }

private:
    ws_parser_t parser_;
    ws_parser_callbacks_t cbs_;

    std::string cur_data_;
    ws_frame_type_t cur_data_type_;

    std::string cur_control_;
    ws_frame_type_t cur_control_type_;

    queue_type q_;
};

RFC6455Decoder::RFC6455Decoder()
    : handshake_(new http::Parser(true, false)),
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
    if (impl_->hasData())
        return impl_->pollFrame();
    while (!buf.empty()) {
        auto front = buf.pop_front();
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
            } else if (handshake_->hasCompeleted()) {
                throw IOError("must be websocket upgrade request");
            }
        } else {
            int nparsed = impl_->execute(front);
            // FUTURES_DLOG(INFO) << nparsed << ", " << front->length();
            if (nparsed != WS_OK)
                throw IOError("invalid ws data frame: " + std::string(ws_parser_error(nparsed)));
            if (impl_->hasData())
                return impl_->pollFrame();
        }
    }
    return none;
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
        uint8_t fsv = 0x80;
        if (out.getType() == DataFrame::TEXT) {
            fsv |= WS_FRAME_TEXT;
        } else if (fsv == DataFrame::CLOSE) {
            fsv |= WS_FRAME_CLOSE;
        } else if (fsv == DataFrame::BINARY) {
            fsv |= WS_FRAME_BINARY;
        } else if (fsv == DataFrame::PING) {
            fsv |= WS_FRAME_PING;
        } else if (fsv == DataFrame::PONG) {
            fsv |= WS_FRAME_PONG;
        } else {
            throw IOError("unimpl");
        }
        folly::io::QueueAppender appender(&buf, 4000);
        appender.write<uint8_t>(fsv);
        encodeLength(appender, out.getData().length());
        appender.push((const uint8_t*)out.getData().data(), out.getData().length());
    }
}

int RFC6455Encoder::encodeLength(folly::io::QueueAppender &appender, size_t length) {
    int num_bytes = 1;
    if(length>=126) {
        if(length>0xffff) {
            num_bytes=8;
            appender.write<uint8_t>(127);
        }
        else {
            num_bytes=2;
            appender.write<uint8_t>(126);
        }

        for(int c=num_bytes-1;c>=0;c--) {
            appender.write<uint8_t>((static_cast<unsigned long long>(length) >> (8 * c)) % 256);
        }
    }
    else
        appender.write<uint8_t>(static_cast<uint8_t>(length));
    return num_bytes;
}


}
}

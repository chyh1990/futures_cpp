#include <futures/io/StreamAdapter.h>
#include <futures/http/HttpCodec.h>
#include <unordered_map>

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
}

#include <futures/http/HttpClient.h>
#include <futures/http/http_parser.h>
#include <futures/dns/ResolverFuture.h>

namespace futures {
namespace http {

static std::string getField(const char *buf,
        const http_parser_url& url, int field) {
    if (url.field_set & (1 << field)) {
        return std::string(buf + url.field_data[field].off,
                url.field_data[field].len);
    } else {
        return "";
    }
}

Url HttpClient::parseUrl(const std::string &host) {
    http_parser_url url;
    http_parser_url_init(&url);
    const char *p = host.c_str();
    int ret = http_parser_parse_url(p, host.size(), false, &url);
    if (ret) throw std::invalid_argument("invalid url");

    Url h;
    h.host = getField(p, url, UF_HOST);
    h.schema = getField(p, url, UF_SCHEMA);
    if (h.schema.empty()) h.schema = "http";
    if ((h.schema != "http") && (h.schema != "https"))
        throw std::invalid_argument("invalid schema");
    bool ssl = h.schema == "https";
    h.port = url.port ? url.port : (ssl ? 443 : 80);
    h.path = getField(p, url, UF_PATH);
    if (h.path.empty()) h.path = "/";
    return h;
}

HttpClient::HttpClient(EventExecutor *ev, dns::AsyncResolver::Ptr resolver, const Url &url)
    : ev_(ev), resolver_(resolver), host_(url) {
}

HttpClient::HttpClient(EventExecutor *ev, io::SSLContext *ctx, dns::AsyncResolver::Ptr resolver, const Url &url)
    : ev_(ev), ssl_ctx_(ctx), resolver_(resolver), host_(url) {
}

BoxedFuture<folly::IPAddress> HttpClient::resolve() {
    if (folly::IPAddress::validate(host_.host)) {
        return makeOk(folly::IPAddress(host_.host));
    }
    return resolver_->resolve(host_.host, dns::AsyncResolver::EnableTypeA4)
        | [] (dns::ResolverResult res) {
            return res[0];
        };
}

void HttpClient::spawnClient(io::SocketChannel::Ptr sock) {
    sock_ = sock;
    client_ = std::make_shared<dispatcher_type>();
    ev_->spawn(makeRpcClientFuture(
        sock,
        io::FramedStream<http::Response>(sock, std::make_shared<http::HttpV1ResponseDecoder>()),
        io::FramedSink<http::Request>(sock, std::make_shared<http::HttpV1RequestEncoder>()),
        client_)
    );
}


BoxedFuture<Unit> HttpClient::connect() {
    if (client_ && sock_->good()) {
        return makeOk();
    } else {
        resetConnection();
    }
    if (isSSL() && !ssl_ctx_)
        throw std::invalid_argument("not support");
    auto self = shared_from_this();
    if (!isSSL()) {
         return (resolve()
             >> [self] (folly::IPAddress ip) {
                 folly::SocketAddress addr(ip, self->host_.port);
                 return io::SocketChannel::connect(self->ev_, addr);
             })
             | [self] (io::SocketChannel::Ptr sock) {
                 self->spawnClient(sock);
                 return unit;
             };
    } else {
        return (resolve()
            >> [self] (folly::IPAddress ip) {
                 folly::SocketAddress addr(ip, self->host_.port);
                 return io::SSLSocketChannel::connect(self->ev_, self->ssl_ctx_, addr);
            })
            | [self] (io::SSLSocketChannel::Ptr sock) {
                self->spawnClient(sock);
                return unit;
            };
    }
}

BoxedFuture<Unit> HttpClient::close() {
    if (!client_ || closing_) return makeOk();
    closing_ = true;
    sock_.reset();
    return client_->close();
}

BoxedFuture<Response> HttpClient::request(http::Request&& req) {
    if (closing_) throw IOError("HttpClient closed");
    auto self = shared_from_this();
    auto w = folly::makeMoveWrapper(std::move(req));
    return connect()
        >> [self, w] (Unit) {
            return (*self->client_)(w.move());
        }
        >> [self] (Response resp) {
            auto it = resp.headers.find("Connection");
            if (it != resp.headers.end() && it->second == "close") {
                FUTURES_DLOG(INFO) << "keep-alive not supported";
                self->resetConnection();
            }
            return makeOk(std::move(resp));
        };
}

void HttpClient::fillHeaders(HttpClient::HeaderFields &headers) {
    headers["Host"] = host_.host;
    if (!user_agent_.empty())
        headers["User-Agent"] = user_agent_;
}

}
}

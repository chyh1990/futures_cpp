#pragma once

#include <futures/http/HttpCodec.h>
#include <futures/io/AsyncSocket.h>
#include <futures/io/AsyncSSLSocket.h>
#include <futures/dns/Resolver.h>
#include <futures/service/RpcFuture.h>
#include <futures/service/ClientDispatcher.h>

namespace futures {
namespace http {

struct Url {
    std::string schema;
    std::string host;
    unsigned short port;
    std::string path;
};

class HttpClient : public std::enable_shared_from_this<HttpClient> {
public:
    using HeaderFields = std::unordered_map<std::string, std::string>;

    HttpClient(EventExecutor *ev, dns::AsyncResolver::Ptr resolver, const Url &url);
    HttpClient(EventExecutor *ev, io::SSLContext *ctx, dns::AsyncResolver::Ptr resolver, const Url &url);

    BoxedFuture<Unit> close();

    BoxedFuture<Unit> connect();

    BoxedFuture<Response> request(http::Request&& req);

    BoxedFuture<Response> get(const std::string &path,
            const HeaderFields& headers = HeaderFields())
    {
        http::Request req;
        req.path = path;
        req.headers = headers;
        fillHeaders(req.headers);
        req.content_length = 0;
        req.method = HTTP_GET;
        return request(std::move(req));
    }

    BoxedFuture<Response> post(const std::string &path,
            std::unique_ptr<folly::IOBuf> content,
            const HeaderFields& headers = HeaderFields())
    {
        http::Request req;
        req.path = path;
        req.headers = headers;
        fillHeaders(req.headers);
        req.method = HTTP_POST;
        req.body.append(std::move(content));
        return request(std::move(req));
    }
    static Url parseUrl(const std::string &host);

    bool isSSL() const { return host_.schema == "https"; }
    bool good() const { return !closing_ && client_; }

    void setUserAgent(const std::string &ua) {
        user_agent_ = ua;
    }

    const std::string &getUserAgent() {
        return user_agent_;
    }
private:
    using dispatcher_type = service::PipelineClientDispatcher<http::Request, http::Response>;
    EventExecutor *ev_;
    io::SSLContext *ssl_ctx_ = nullptr;
    dns::AsyncResolver::Ptr resolver_;
    const Url host_;

    // settings
    std::string user_agent_{"HttpClientCpp/0.1.0"};

    std::shared_ptr<dispatcher_type> client_;
    io::SocketChannel::Ptr sock_;
    bool closing_ = false;

    BoxedFuture<folly::IPAddress> resolve();

    void spawnClient(io::SocketChannel::Ptr sock);
    void fillHeaders(HeaderFields &headers);

    void resetConnection() {
        if (client_)
            client_->close();
        client_.reset();
        sock_.reset();
    }
};

}
}

#pragma once

#include <stdexcept>

typedef struct ssl_ctx_st SSL_CTX;

namespace futures {
namespace io {

class SSLException : public std::runtime_error {
public:
    explicit SSLException(const std::string& err)
        : std::runtime_error(err) {}

    explicit SSLException(const char *err)
	: std::runtime_error(err) {}
};

class SSLContext {
public:
    SSLContext();
    ~SSLContext();

    SSLContext(SSLContext&& ctx) = delete;
    SSLContext& operator=(SSLContext&& ctx) = delete;
    SSLContext(const SSLContext& ctx) = delete;
    SSLContext& operator=(SSLContext& ctx) = delete;

    SSL_CTX *getRaw() { return ctx_; }
private:
    SSL_CTX *ctx_;
};

}
}

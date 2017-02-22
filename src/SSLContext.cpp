#include <futures/io/SSLContext.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#if (SSLEAY_VERSION_NUMBER >= 0x0907000L)
# include <openssl/conf.h>
#endif

namespace futures {
namespace io {

static void init_openssl_library(void)
{
    OpenSSL_add_all_algorithms();
    ERR_load_BIO_strings();
    ERR_load_crypto_strings();
    SSL_load_error_strings();

    (void)SSL_library_init();

}

class SSLInitOnce {
public:
    SSLInitOnce() { init_openssl_library(); }
};

static SSLInitOnce _init_once;

SSLContext::SSLContext()
    : ctx_(nullptr)
{
    const SSL_METHOD* method = SSLv23_method();
    if (!method)
        throw SSLException("SSLv23_method");

    ctx_ = SSL_CTX_new(method);
    if (!ctx_)
        throw SSLException("SSL_CTX_new");
    SSL_CTX_set_verify_depth(ctx_, 4);

    const long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
    SSL_CTX_set_options(ctx_, flags);

    // SSL_CTX_set_mode(ctx_, SSL_MODE_AUTO_RETRY);

    SSL_CTX_set_mode(ctx_,
        SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
        | SSL_MODE_ENABLE_PARTIAL_WRITE);
}

SSLContext::~SSLContext() {
    if (ctx_)
        SSL_CTX_free(ctx_);
}

}
}

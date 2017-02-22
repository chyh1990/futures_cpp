#pragma once

#include <futures/io/AsyncSocket.h>
#include <futures/io/SSLContext.h>

typedef struct ssl_st SSL;

namespace futures {
namespace io {

namespace detail {

}

class SSLSocketChannel : public SocketChannel {
public:
    using Ptr = std::shared_ptr<SSLSocketChannel>;

    SSLSocketChannel(EventExecutor *ev, SSLContext *ctx);
    ~SSLSocketChannel();
    void onEvent(ev::io& watcher, int revent);

    io::intrusive_ptr<ConnectCompletionToken> doHandshake();
    void printPeerCert();
private:
    enum SSLState {
        STATE_UNINIT,
        STATE_UNENCRYPTED,
        STATE_CONNECTING,
        STATE_ESTABLISHED,
        STATE_ERROR,
        STATE_CLOSED,
    };

    SSL *ssl_;
    SSLState ssl_state_ = STATE_UNINIT;
    size_t minWriteSize_{1500};

    bool willBlock(int ret, int *sslErr, unsigned long *errOut);
    void failHandshake(std::error_code ec);
    void handleConnect();

    ssize_t performWrite(
            const iovec* vec,
            size_t count,
            size_t* countWritten,
            size_t* partialWritten,
            std::error_code &ec) override;

    ssize_t performRead(ReaderCompletionToken *tok, std::error_code &ec) override;
    ssize_t realPerformRead(void *buf, size_t bufLen, std::error_code &ec);

    int eorAwareSSLWrite(SSL *ssl, const void *buf, int n, bool eor);
    std::error_code interpretSSLError(int rc, int error);

};

class HandshakeFuture : public FutureBase<HandshakeFuture, folly::Unit> {
public:
    using Item = folly::Unit;

    HandshakeFuture(SSLSocketChannel::Ptr ptr)
        : ptr_(ptr) {}

    Poll<Item> poll() override {
        if (!tok_)
            tok_ = ptr_->doHandshake();
        return tok_->poll();
    }
private:
    SSLSocketChannel::Ptr ptr_;
    io::intrusive_ptr<SocketChannel::ConnectCompletionToken> tok_;
};



}
}

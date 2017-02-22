#pragma once

#include <futures/io/AsyncSocket.h>
#include <futures/io/SSLContext.h>

typedef struct ssl_st SSL;

namespace futures {
namespace io {

class SSLSockConnectFuture;

class SSLSocketChannel : public SocketChannel {
public:
    using Ptr = std::shared_ptr<SSLSocketChannel>;

    SSLSocketChannel(EventExecutor *ev, SSLContext *ctx);
    ~SSLSocketChannel();
    void onEvent(ev::io& watcher, int revent);

    io::intrusive_ptr<ConnectCompletionToken> doHandshake();
    void printPeerCert();

    static SSLSockConnectFuture
        connect(EventExecutor *ev, SSLContext *ctx, const folly::SocketAddress &addr);
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

    ssize_t performRead(void *buf, size_t bufLen, std::error_code &ec) override;

    int eorAwareSSLWrite(SSL *ssl, const void *buf, int n, bool eor);
    std::error_code interpretSSLError(int rc, int error);

};

class SSLSockConnectFuture : public FutureBase<SSLSockConnectFuture, SSLSocketChannel::Ptr> {
public:
    using Item = SSLSocketChannel::Ptr;

    SSLSockConnectFuture(EventExecutor *ev, SSLContext *ctx, const folly::SocketAddress &addr)
        : ptr_(std::make_shared<SSLSocketChannel>(ev, ctx)), addr_(addr) {
    }

    Poll<Item> poll() override {
    again:
        switch (s_) {
            case INIT:
                conn_tok_ = ptr_->doConnect(addr_);
                s_ = CONN;
                goto again;
            case CONN: {
                auto r = conn_tok_->poll();
                if (r.hasException())
                    return Poll<Item>(r.exception());
                if (r->isReady()) {
                    conn_tok_ = ptr_->doHandshake();
                    s_ = HANDSHAKE;
                    goto again;
                } else {
                    return Poll<Item>(not_ready);
                }
            }
            case HANDSHAKE: {
                auto r = conn_tok_->poll();
                if (r.hasException())
                    return Poll<Item>(r.exception());
                if (r->isReady()) {
                    s_ = DONE;
                    return makePollReady(std::move(ptr_));
                } else {
                    return Poll<Item>(not_ready);
                }
            }
            default:
                throw InvalidPollStateException();
        }
    }
private:
    enum State {
        INIT,
        CONN,
        HANDSHAKE,
        DONE,
    };
    State s_ = INIT;
    SSLSocketChannel::Ptr ptr_;
    folly::SocketAddress addr_;
    io::intrusive_ptr<SocketChannel::ConnectCompletionToken> conn_tok_;
};



}
}

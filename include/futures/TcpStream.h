#pragma once

#include <futures/core/IOBuf.h>
#include <futures/Stream.h>
#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>
#include <futures/io/IoFuture.h>

namespace futures {

namespace tcp {

class Socket {
public:
    Socket();
    // take ownership
    Socket(int fd) : fd_(fd) {}
    ~Socket();

    bool connect(const std::string &addr, uint16_t port, std::error_code &ec) ;
    bool is_connected(std::error_code &ec);

    void tcpServer(const std::string& bindaddr, uint16_t port, int backlog, std::error_code &ec);

    void close() noexcept;
    ssize_t writev(const iovec *vec, size_t veclen, int flags, std::error_code &ec);
    ssize_t recv(void *buf, size_t len, int flags, std::error_code &ec);
    Socket accept(std::error_code& ec);

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& o)
        : fd_(o.fd_) {
        o.fd_ = -1;
    }

    Socket& operator=(Socket&& o) noexcept {
        if (this == &o)
            return *this;
        close();
        fd_ = o.fd_;
        o.fd_ = -1;
        return *this;
    }

    int fd() const { return fd_; }
    bool isValid() const { return fd_ >= 0; }
private:
    int fd_;
};

using SocketPtr = std::shared_ptr<Socket>;

class SocketIOHandler : public io::DescriptorIo {
public:
    SocketIOHandler(EventExecutor* reactor, SocketPtr sock)
        : io::DescriptorIo(reactor, sock->fd()), socket_(sock) {
        FUTURES_DLOG(INFO) << "SocketIOHandler new";
    }

    ~SocketIOHandler() {
        if (socket_)
            FUTURES_DLOG(INFO) << "SocketIOHandler delete";
    }

    ssize_t read(void *buf, size_t len, std::error_code &ec) override {
        return socket_->recv(buf, len, 0, ec);
    }

    ssize_t writev(const iovec *vec, size_t veclen, std::error_code &ec) override {
        return socket_->writev(vec, veclen, 0, ec);
    }

    Socket *getSocket() { return socket_.get(); }
    SocketPtr getSocketPtr() { return socket_; }

private:
    std::shared_ptr<Socket> socket_;
};

class ConnectFuture : public FutureBase<ConnectFuture, SocketPtr> {
public:
    enum State {
        INIT,
        CONNECTING,
        CONNECTED,
        CANCELLED,
    };

    using Item = SocketPtr;

    Poll<SocketPtr> poll() override;
    void cancel() override;

    ConnectFuture(EventExecutor *ev,
        const std::string addr, uint16_t port)
        : io_(new SocketIOHandler(ev, std::make_shared<Socket>())),
        addr_(addr), port_(port) {}

private:
    std::unique_ptr<SocketIOHandler> io_;
    State s_ = INIT;
    std::string addr_;
    uint16_t port_;
};

class AcceptStream : public StreamBase<AcceptStream, SocketPtr> {
public:
    enum State {
        INIT,
        ACCEPTING,
        CLOSED,
    };

    using Item = SocketPtr;

    Poll<Optional<Item>> poll() override;

    AcceptStream(EventExecutor *ev, std::shared_ptr<Socket> s)
        : io_(new SocketIOHandler(ev, s)) {}

private:
    State s_ = INIT;
    std::unique_ptr<SocketIOHandler> io_;
};

class Stream {
public:
    static ConnectFuture connect(EventExecutor *reactor,
            const std::string &addr, uint16_t port)
    {
        return ConnectFuture(reactor, addr, port);
    }

    static io::SendFuture send(EventExecutor *reactor,
            SocketPtr socket, std::unique_ptr<folly::IOBuf> buf)
    {
        return io::SendFuture(folly::make_unique<SocketIOHandler>(reactor, socket),
                std::move(buf));
    }

    static AcceptStream acceptStream(EventExecutor *reactor,
            std::shared_ptr<Socket> listener)
    {
        return AcceptStream(reactor, listener);
    }

    template <class ReadPolicy>
    static io::RecvFuture<ReadPolicy> recv(EventExecutor *reactor,
            std::shared_ptr<Socket> socket, ReadPolicy&& policy)
    {
        return io::RecvFuture<ReadPolicy>(
               folly::make_unique<SocketIOHandler>(reactor, socket),
               std::forward<ReadPolicy>(policy));
    }

};


}
}

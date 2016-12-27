#pragma once

#include <futures/core/IOBuf.h>
#include <futures/Future.h>
#include <futures/Stream.h>
#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>

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
    ssize_t send(const void *buf, size_t len, int flags, std::error_code &ec);
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

class SocketIOHandler {
private:
    ev::io io_;
    Task task_;
    EventExecutor &reactor_;

public:
    SocketIOHandler(EventExecutor& reactor, Task task, int fd, int mask)
        : io_(reactor.getLoop()), task_(task), reactor_(reactor) {
        std::cerr << "SocketIOHandlerHERE: " << std::endl;
        io_.set(this);
        reactor_.incPending();
        io_.start(fd, mask);
    }

    void operator()(ev::io &io, int revents) {
        std::cerr << "SocketIOHandler() " << revents << std::endl;
        task_.unpark();
    }

    ~SocketIOHandler() {
        std::cerr << "SocketIOHandlerDelete: " << std::endl;
        reactor_.decPending();
        io_.stop();
    }

};

class SocketFutureMixin {
public:
    void register_fd(int mask);
    void unregister_fd();

    SocketFutureMixin(EventExecutor &ev, Socket socket)
        : reactor_(ev), socket_(std::move(socket)) {
    }

    // ~SocketFutureMixin() {
    //     unregister_fd();
    // }

protected:
    EventExecutor &reactor_;
    Socket socket_;
    std::unique_ptr<SocketIOHandler> handler_;
};

class ConnectFuture : public FutureBase<ConnectFuture, Socket>, SocketFutureMixin {
public:
    enum State {
        INIT,
        CONNECTING,
        CONNECTED,
        CANCELLED,
    };

    typedef Socket Item;

    Poll<Socket> poll() override;
    void cancel() override;

    ConnectFuture(EventExecutor &ev,
        const std::string addr, uint16_t port)
        : SocketFutureMixin(ev, Socket()), s_(INIT),
        addr_(addr), port_(port) {}

private:
    State s_;
    std::string addr_;
    uint16_t port_;
};

typedef std::tuple<Socket, ssize_t> SendFutureItem;
class SendFuture : public FutureBase<SendFuture, SendFutureItem>, SocketFutureMixin {
public:
    typedef SendFutureItem Item;

    enum State {
        INIT,
        SENT,
        CANCELLED,
    };

    SendFuture(EventExecutor &ev, Socket socket,
            std::unique_ptr<folly::IOBuf> buf)
        : SocketFutureMixin(ev, std::move(socket)), s_(INIT),
        buf_(std::move(buf)) {}

    Poll<Item> poll() override;
    void cancel() override;

private:
    State s_;
    std::unique_ptr<folly::IOBuf> buf_;
};

class TransferAtLeast {
public:
    TransferAtLeast(ssize_t length, ssize_t buf_size)
        : length_(length), buf_size_(buf_size) {
        assert(length > 0);
        assert(buf_size >= length);
    }

    TransferAtLeast(ssize_t length)
        : length_(length), buf_size_(length * 2) {
        assert(length >= 0);
    }

    size_t bufferSize() const {
        return buf_size_;
    }

    size_t remainBufferSize() const {
        assert(buf_size_ >= read_);
        return buf_size_ - read_;
    }

    // mark readed
    bool read(ssize_t s) {
        assert(s >= 0);
        read_ += s;
        if (read_ >= length_)
            return true;
        return false;
    }

private:
    const ssize_t length_;
    const ssize_t buf_size_;
    ssize_t read_ = 0;
};

class TransferExactly : public TransferAtLeast {
public:
    TransferExactly(ssize_t size)
        : TransferAtLeast(size, size) {}
};

typedef std::tuple<Socket, std::unique_ptr<folly::IOBuf>> RecvFutureItem;
template <class ReadPolicy>
class RecvFuture
    : public FutureBase<RecvFuture<ReadPolicy>, RecvFutureItem>,
             SocketFutureMixin {
public:
    typedef RecvFutureItem Item;

    enum State {
        INIT,
        RECV,
        CANCELLED,
    };

    RecvFuture(EventExecutor &ev, Socket socket, const ReadPolicy& policy)
        : SocketFutureMixin(ev, std::move(socket)), s_(INIT),
        policy_(policy),
        buf_(folly::IOBuf::create(policy_.bufferSize())) {}

    Poll<Item> poll() override;
    void cancel() override;

private:
    State s_;
    ReadPolicy policy_;
    std::unique_ptr<folly::IOBuf> buf_;
    // ssize_t length_to_read_;
};

class AcceptStream : public StreamBase<AcceptStream, Socket> {
public:
    enum State {
        INIT,
        ACCEPTING,
        CLOSED,
    };

    using Item = Socket;

    Poll<Optional<Item>> poll() override;

    AcceptStream(EventExecutor &ev, Socket& s)
        : ev_(ev), socket_(s) {}
private:
    State s_ = INIT;
    EventExecutor &ev_;
    Socket &socket_;
    std::unique_ptr<SocketIOHandler> handler_;
};

class Stream {
public:
    static ConnectFuture connect(EventExecutor &reactor,
            const std::string &addr, uint16_t port)
    {
        return ConnectFuture(reactor, addr, port);
    }

    static SendFuture send(EventExecutor &reactor,
            Socket socket, std::unique_ptr<folly::IOBuf> buf)
    {
        return SendFuture(reactor, std::move(socket), std::move(buf));
    }

    static AcceptStream acceptStream(EventExecutor &reactor, Socket &listener) {
        return AcceptStream(reactor, listener);
    }

    template <class ReadPolicy>
    static RecvFuture<ReadPolicy> recv(EventExecutor &reactor,
            Socket socket, ReadPolicy&& policy)
    {
        return RecvFuture<ReadPolicy>(reactor, std::move(socket), std::forward<ReadPolicy>(policy));
    }

};


}
}

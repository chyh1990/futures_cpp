#pragma once

#include <system_error>
#include <futures/core/IOBuf.h>
#include <futures/Future.h>
#include <futures/EventLoop.h>
#include <futures/EventExecutor.h>

namespace futures {

namespace tcp {

class Socket {
public:
    Socket();
    ~Socket();

    bool connect(const std::string &addr, uint16_t port, std::error_code &ec) ;
    bool is_connected(std::error_code &ec);

    void close() noexcept;
    ssize_t send(const void *buf, size_t len, int flags, std::error_code &ec);
    ssize_t recv(void *buf, size_t len, int flags, std::error_code &ec);

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
private:
    int fd_;
};

class SocketIOHandler {
private:
    ev::io io_;
    Task task_;

public:
    SocketIOHandler(EventExecutor& reactor, Task task, int fd, int mask)
        : io_(reactor.getLoop()), task_(task) {
        std::cerr << "SocketIOHandlerHERE: " << std::endl;
        io_.set(this);
        io_.start(fd, mask);
    }

    void operator()(ev::io &io, int revents) {
        std::cerr << "SocketIOHandler() " << revents << std::endl;
        task_.unpark();
        delete this;
    }

protected:
    ~SocketIOHandler() {
        std::cerr << "SocketIOHandlerDelete: " << std::endl;
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
    bool registered_ = false;
};

class ConnectFuture : public FutureBase<ConnectFuture, Socket>, SocketFutureMixin {
public:
    enum State {
        INIT,
        CONNECTING,
        CONNECTED,
    };

    typedef Socket Item;

    Poll<Socket> poll();

    ConnectFuture(EventExecutor &ev,
        const std::string addr, uint16_t port)
        : SocketFutureMixin(ev, Socket()), s_(INIT),
        addr_(addr), port_(port) {}

private:
    State s_;
    std::string addr_;
    uint16_t port_;
};

typedef std::pair<Socket, ssize_t> SendFutureItem;
class SendFuture : public FutureBase<SendFuture, SendFutureItem>, SocketFutureMixin {
public:
    typedef SendFutureItem Item;

    enum State {
        INIT,
        SENT,
    };

    SendFuture(EventExecutor &ev, Socket socket,
            std::unique_ptr<folly::IOBuf> buf)
        : SocketFutureMixin(ev, std::move(socket)), s_(INIT),
        buf_(std::move(buf)) {}

    Poll<Item> poll();

private:
    State s_;
    std::unique_ptr<folly::IOBuf> buf_;
};

typedef std::pair<Socket, std::unique_ptr<folly::IOBuf>> RecvFutureItem;
class RecvFuture : public FutureBase<RecvFuture, RecvFutureItem>, SocketFutureMixin {
public:
    typedef RecvFutureItem Item;

    enum State {
        INIT,
        RECV,
    };

    RecvFuture(EventExecutor &ev, Socket socket, size_t length)
        : SocketFutureMixin(ev, std::move(socket)), s_(INIT),
        buf_(folly::IOBuf::create(length)), length_to_read_(length) {}

    Poll<Item> poll();

private:
    State s_;
    std::unique_ptr<folly::IOBuf> buf_;
    ssize_t length_to_read_;
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

    static RecvFuture recv(EventExecutor &reactor,
            Socket socket, size_t length)
    {
        return RecvFuture(reactor, std::move(socket), length);
    }

};

}
}

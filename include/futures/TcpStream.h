#pragma once

#include <system_error>
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

class SocketIOHandler : public FileEventHandler {
public:
    SocketIOHandler(EventLoop *ev, Task task)
        : FileEventHandler(ev), task_(task) {
        std::cerr << "SocketIOHandlerHERE: " << std::endl;
    }

    void operator()(int fd, int mask) {
        task_.unpark();
        delete this;
    }

protected:
    ~SocketIOHandler() {
        std::cerr << "SocketIOHandlerDelete: " << std::endl;
    }

private:
    Task task_;
    bool registered_ = false;
};

class SocketFutureMixin {
public:
    void register_fd();
    void unregister_fd();

    SocketFutureMixin(EventExecutor &ev, Socket socket)
        : reactor_(ev), socket_(std::move(socket)) {}

protected:
    EventExecutor &reactor_;
    Socket socket_;

private:
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
            const std::string &buf)
        : SocketFutureMixin(ev, std::move(socket)), s_(INIT),
        buf_(buf) {}

    Poll<Item> poll();

private:
    State s_;
    std::string buf_;

};

class Stream {
public:
    static ConnectFuture connect(EventExecutor &reactor,
            const std::string &addr, uint16_t port)
    {
        return ConnectFuture(reactor, addr, port);
    }

    static SendFuture send(EventExecutor &reactor,
            Socket socket, const std::string &buf)
    {
        return SendFuture(reactor, std::move(socket), buf);
    }
};

}
}

#include <futures/TcpStream.h>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>

extern "C" {
#include "libae/anet.h"
}

namespace futures {
namespace tcp {

inline std::error_code current_system_error(int e = errno) {
    return std::error_code(e, std::system_category());
}

Socket::Socket()
    : fd_(-1) {
}

Socket::~Socket() {
    close();
}

void Socket::close() noexcept {
    if (fd_ >= 0) {
        FUTURES_DLOG(INFO) << "close fd: " << fd_;
        ::close(fd_);
    }
    fd_ = -1;
}

bool Socket::connect(const std::string &addr, uint16_t port, std::error_code &ec)
{
    char buf[ANET_ERR_LEN];
    char addr_buf[256];
    if (addr.size() > 255) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    strcpy(addr_buf, addr.c_str());
    fd_ = anetTcpNonBlockConnect(buf, addr_buf, port);
    if (fd_ < 0) {
        ec = current_system_error();
        return false;
    }
    if (errno == EINPROGRESS)
        return false;
    return true;
}

bool Socket::is_connected(std::error_code &ec)
{
    int result;
    socklen_t result_len = sizeof(result);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
        ec = current_system_error();
        return false;
    }
    if (result == 0) {
        return true;
    } else if(result == EINPROGRESS) {
        return false;
    } else {
        ec = current_system_error(result);
        return false;
    }
}

ssize_t Socket::send(const void *buf, size_t len, int flags, std::error_code &ec)
{
    assert(fd_ >= 0);
again:
    ssize_t sent = ::send(fd_, buf, len, flags);
    if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        if (errno == EINTR)
            goto again;
        ec = current_system_error();
        return 0;
    } else {
        return sent;
    }
}

ssize_t Socket::recv(void *buf, size_t len, int flags, std::error_code &ec)
{
    assert(fd_ >= 0);
again:
    ssize_t l = ::recv(fd_, buf, len, flags);
    if (l == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        if (errno == EINTR)
            goto again;
        ec = current_system_error();
        return 0;
    } else if (l == 0) {
        // closed by peer
        ec = std::make_error_code(std::errc::connection_aborted);
        return 0;
    } else {
        return l;
    }
}

void Socket::tcpServer(const std::string& bindaddr, uint16_t port,
        int backlog, std::error_code &ec) {
    assert(fd_ < 0);

    char buf[ANET_ERR_LEN];
    char addr_buf[256];
    if (bindaddr.size() > 255) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    strcpy(addr_buf, bindaddr.c_str());
    int fd = anetTcpServer(buf, port, addr_buf, backlog);
    if (fd < 0) {
        ec = current_system_error();
        return;
    }
    fd_ = fd;
    if (anetNonBlock(buf, fd_)) {
        close();
        ec = current_system_error();
        return;
    }
}

Socket Socket::accept(std::error_code& ec) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    int fd;
again:
    fd = ::accept(fd_, (struct sockaddr*)&sa, &salen);
    if (fd == -1) {
        if (errno == EINTR) {
            goto again;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Socket();
        } else {
            ec = current_system_error();
            return Socket();
        }
    } else {
        char buf[ANET_ERR_LEN];
        if (anetNonBlock(buf, fd)) {
            ec = current_system_error();
            ::close(fd);
            return Socket();
        }
        return Socket(fd);
    }
}

Poll<Socket> ConnectFuture::poll() {
    std::error_code ec;
    switch (s_) {
        case INIT: {
            bool b = socket_.connect(addr_, port_, ec);
            if (ec) {
                return Poll<Socket>(IOError("connect", ec));
            } else if (b) {
                s_ = CONNECTED;
                return Poll<Socket>(Async<Socket>(std::move(socket_)));
            } else {
                register_fd(ev::WRITE);
                s_ = CONNECTING;
            }
            break;
        }
        case CONNECTING: {
            bool b = socket_.is_connected(ec);
            if (ec) {
                unregister_fd();
                return Poll<Socket>(IOError("is_connect", ec));
            }
            if (b) {
                unregister_fd();
                s_ = CONNECTED;
                return Poll<Socket>(Async<Socket>(std::move(socket_)));
                // connected
            }
            break;
        }
        case CANCELLED:
            return Poll<Socket>(FutureCancelledException());
        default:
            throw InvalidPollStateException();
    }

    return Poll<Socket>(not_ready);
}

void ConnectFuture::cancel() {
    handler_.reset();
    s_ = CANCELLED;
}

void SocketFutureMixin::register_fd(int mask) {
    if (handler_) return;
    handler_.reset(new SocketIOHandler(reactor_, *CurrentTask::current_task(),
            socket_.fd(), mask));
}

void SocketFutureMixin::unregister_fd() {
    handler_.reset();
}

Poll<SendFuture::Item> SendFuture::poll() {
    std::error_code ec;
    switch (s_) {
        case INIT: {
            // register event
            ssize_t len = socket_.send(buf_->data(), buf_->length(), MSG_NOSIGNAL, ec);
            if (ec) {
                unregister_fd();
                return Poll<Item>(IOError("send", ec));
            } else if (len == 0) {
                register_fd(ev::WRITE);
                s_ = INIT;
            } else {
                s_ = SENT;
                unregister_fd();
                return Poll<Item>(Async<Item>(std::make_tuple(std::move(socket_), len)));
            }
            break;
        }
        case CANCELLED:
            return Poll<Item>(FutureCancelledException());
        default:
            throw InvalidPollStateException();
    }

    return Poll<Item>(not_ready);
}

void SendFuture::cancel() {
    handler_.reset();
    s_ = CANCELLED;
}

template <class ReadPolicy>
Poll<RecvFutureItem> RecvFuture<ReadPolicy>::poll() {
    std::error_code ec;
    switch (s_) {
        case INIT: {
            // register event
            ssize_t len = socket_.recv(buf_->writableTail(), policy_.remainBufferSize(), 0, ec);
            FUTURES_DLOG(INFO) << "S " << socket_.fd() << ", LEN " << len;
            if (ec) {
                unregister_fd();
                return Poll<Item>(IOError("recv", ec));
            } else if (len == 0) {
                register_fd(ev::READ);
                s_ = INIT;
            } else {
                buf_->append(len);
                if (policy_.read(len)) {
                    s_ = DONE;
                    unregister_fd();
                    return Poll<Item>(Async<Item>(std::make_tuple(std::move(socket_), std::move(buf_))));
                } else {
                    s_ = INIT;
                }
            }
            break;
        }
        case CANCELLED:
            return Poll<Item>(FutureCancelledException());
        default:
            throw InvalidPollStateException();
    }

    return Poll<Item>(not_ready);
}

template <typename ReadPolicy>
void RecvFuture<ReadPolicy>::cancel() {
    handler_.reset();
    s_ = CANCELLED;
}

template class RecvFuture<TransferAtLeast>;
template class RecvFuture<TransferExactly>;

Poll<Optional<Socket>> AcceptStream::poll() {
    switch (s_) {
    case INIT:
        handler_.reset(new SocketIOHandler(ev_,
            *CurrentTask::current_task(),
            socket_.fd(), ev::READ));
        s_ = ACCEPTING;
        // fall through
    case ACCEPTING: {
        // libev is level-triggered, just need to accept once
        std::error_code ec;
        Socket s = socket_.accept(ec);
        if (ec) {
            handler_.reset();
            return Poll<Optional<Socket>>(IOError("accept", ec));
        }
        if (s.isValid())
            return makePollReady(folly::make_optional(std::move(s)));
        break;
    }
    default:
        throw InvalidPollStateException();
    }
    return Poll<Optional<Item>>(not_ready);
}

}
}

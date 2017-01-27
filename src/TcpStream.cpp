#include <futures/TcpStream.h>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <climits>

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

void Socket::shutdown(int how, std::error_code &ec) noexcept {
    if (fd_ >= 0) {
        if (::shutdown(fd_, how) < 0)
            ec = current_system_error();
    }
}

bool Socket::connect(const folly::SocketAddress &addr, std::error_code &ec)
{
    return connect(addr.getAddressStr(), addr.getPort(), ec);
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

ssize_t Socket::writev(const iovec *vec, size_t veclen, int flags, std::error_code &ec)
{
    assert(fd_ >= 0);
    struct msghdr msg;
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = const_cast<iovec *>(vec);
    msg.msg_iovlen = std::min<size_t>(veclen, IOV_MAX);
    msg.msg_control = nullptr;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    int msg_flags = MSG_DONTWAIT;

    msg_flags |= MSG_NOSIGNAL;

again:
    ssize_t sent = ::sendmsg(fd_, &msg, msg_flags);
    if (sent < 0) {
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
    int fd = anetTcpServer(buf, port, addr_buf, backlog, false);
    if (fd < 0) {
        ec = current_system_error();
        return;
    }
    fd_ = fd;
    if (anetNonBlock(buf, fd)) {
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
        if (anetEnableTcpNoDelay(buf, fd)) {
            ec = current_system_error();
            ::close(fd);
            return Socket();
        }
        return Socket(fd);
    }
}

Poll<SocketPtr> ConnectFuture::poll() {
    std::error_code ec;
    switch (s_) {
        case INIT: {
                       bool b = io_->getSocket()->connect(addr_, port_, ec);
                       if (ec) {
                           return Poll<Item>(IOError("connect", ec));
                       } else if (b) {
                           s_ = CONNECTED;
                           auto p = io_->getSocketPtr();
                           io_.reset();
                           return makePollReady(p);
                       } else {
                           io_->poll_read();
                           s_ = CONNECTING;
                       }
                       break;
                   }
        case CONNECTING: {
                             assert(io_);
                             bool b = io_->getSocket()->is_connected(ec);
                             if (ec) {
                                 io_.reset();
                                 return Poll<Item>(IOError("is_connect", ec));
                             }
                             if (b) {
                                 auto p = io_->getSocketPtr();
                                 io_.reset();
                                 s_ = CONNECTED;
                                 return makePollReady(p);
                                 // connected
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

void ConnectFuture::cancel() {
    io_.reset();
    s_ = CANCELLED;
}

Poll<Optional<SocketPtr>> AcceptStream::poll() {
    switch (s_) {
        case INIT:
            s_ = ACCEPTING;
            // fall through
        case ACCEPTING: {
                            // libev is level-triggered, just need to accept once
                            std::error_code ec;
                            Socket s = io_->getSocket()->accept(ec);
                            if (ec) {
                                io_.reset();
                                return Poll<Optional<Item>>(IOError("accept", ec));
                            }
                            if (s.isValid()) {
                                auto p = std::make_shared<Socket>(std::move(s));
                                return makePollReady(folly::make_optional(p));
                            } else {
                                io_->poll_read();
                            }
                            break;
                        }
        default:
                        throw InvalidPollStateException();
    }
    return Poll<Optional<Item>>(not_ready);
}

}
}

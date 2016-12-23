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
        std::cerr << "close " << fd_ << std::endl;
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
    std::cerr << "OPT " << result << std::endl;
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
    ssize_t sent = ::send(fd_, buf, len, flags);
    if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        ec = current_system_error();
        return 0;
    } else {
        return sent;
    }
}

ssize_t Socket::recv(void *buf, size_t len, int flags, std::error_code &ec)
{
    assert(fd_ >= 0);
    ssize_t l = ::recv(fd_, buf, len, flags);
    if (l == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        ec = current_system_error();
        return 0;
    } else {
        return l;
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
                register_fd(EV_WRITE);
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
        default:
            throw InvalidPollStateException();
    }

    return Poll<Socket>(not_ready);
}

void SocketFutureMixin::register_fd(int mask) {
    if (registered_) return;
    new SocketIOHandler(reactor_, *CurrentTask::current_task(),
            socket_.fd(), mask);
    reactor_.incPending();
    registered_ = true;
}

void SocketFutureMixin::unregister_fd() {
    if (!registered_) return;
    reactor_.decPending();
    registered_ = false;
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
                register_fd(EV_WRITE);
                s_ = INIT;
            } else {
                s_ = SENT;
                unregister_fd();
                return Poll<Item>(Async<Item>(std::make_pair(std::move(socket_), len)));
            }
            break;
        }
        default:
            throw InvalidPollStateException();
    }

    return Poll<Item>(not_ready);
}

Poll<RecvFuture::Item> RecvFuture::poll() {
    std::error_code ec;
    switch (s_) {
        case INIT: {
            // register event
            ssize_t len = socket_.recv(buf_->writableData(), length_to_read_, 0, ec);
            if (ec) {
                unregister_fd();
                return Poll<Item>(IOError("recv", ec));
            } else if (len == 0) {
                std::cerr << "R " << std::endl;
                register_fd(EV_READ);
                s_ = INIT;
            } else {
                s_ = RECV;
                unregister_fd();
                buf_->append(len);
                return Poll<Item>(Async<Item>(std::make_pair(std::move(socket_), std::move(buf_))));
            }
            break;
        }
        default:
            throw InvalidPollStateException();
    }

    return Poll<Item>(not_ready);
}



}
}

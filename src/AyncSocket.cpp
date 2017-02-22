#include <futures/io/AsyncSocket.h>

namespace futures {
namespace io {

bool SocketChannel::startConnect(std::error_code &ec) {
    bool r = socket_.connect(peer_addr_, ec);
    if (!ec) {
        s_ = CONNECTING;
    } else {
        return false;
    }
    wio_.set(socket_.fd(), ev::WRITE);
    rio_.set(socket_.fd(), ev::READ);
    wio_.start();
    return r;
}

ssize_t SocketChannel::performWrite(
        const iovec* vec,
        size_t count,
        size_t* countWritten,
        size_t* partialWritten,
        std::error_code &ec) {
    ssize_t totalWritten = socket_.writev(vec, count, 0, ec);
    if (ec) return 0;

    size_t bytesWritten;
    size_t n;
    for (bytesWritten = totalWritten, n = 0; n < count; ++n) {
        const iovec* v = vec + n;
        if (v->iov_len > bytesWritten) {
            // Partial write finished in the middle of this iovec
            *countWritten = n;
            *partialWritten = bytesWritten;
            return totalWritten;
        }

        bytesWritten -= v->iov_len;
    }

    assert(bytesWritten == 0);
    *countWritten = n;
    *partialWritten = 0;
    return totalWritten;
}

ssize_t SocketChannel::performRead(void* buf, size_t buflen, std::error_code &ec) {
    ssize_t r = socket_.recv(buf, buflen, 0, ec);
    if (!ec) {
        return r == 0 ? READ_EOF : r;
    } else if (ec == std::make_error_code(std::errc::operation_would_block)) {
        return READ_WOULDBLOCK;
    } else {
        return READ_ERROR;
    }
}

ssize_t SocketChannel::handleRead(ReaderCompletionToken *tok, std::error_code &ec) {
    const static size_t kMaxReadPerEvent = 12;
    size_t reads = 0;
    while (reads < kMaxReadPerEvent) {
        void *buf;
        size_t bufLen = 0;
        tok->prepareBuffer(&buf, &bufLen);
        assert(buf);
        assert(bufLen > 0);
        ssize_t read_ret = performRead(buf, bufLen, ec);
        FUTURES_DLOG(INFO) << "readed: " << read_ret;
        if (read_ret == READ_ERROR) {
            tok->readError(ec);
            return read_ret;
        } else if (read_ret == READ_WOULDBLOCK) {
            // XXX not needed?
            tok->dataReady(0);
            return read_ret;
        } else if (read_ret == READ_EOF) {
            FUTURES_DLOG(INFO) << "Socket EOF";
            tok->readEof();
            return read_ret;
        } else {
            tok->dataReady(read_ret);
            reads++;
            if (read_ret < bufLen) {
                return read_ret;
            }
        }
    }
    return READ_WOULDBLOCK;
}

void SocketChannel::handleInitialReadWrite() {
    if (getPending(IOObject::OpRead).empty())
        rio_.stop();
    else
        rio_.start();

    if (getPending(IOObject::OpWrite).empty())
        wio_.stop();
    else
        wio_.start();
}

void SocketChannel::onEvent(ev::io& watcher, int revent) {
    if (revent & ev::ERROR)
        throw std::runtime_error("syscall error");
    if (revent & ev::READ) {
        if (s_ == CONNECTED) {
            auto &reader = getPending(IOObject::OpRead);
            if (!reader.empty()) {
                auto first = static_cast<ReaderCompletionToken*>(&reader.front());
                std::error_code ec;
                ssize_t ret = handleRead(first, ec);
                if (ret == READ_EOF) {
                    // todo
                    closeRead();
                } else if (ret == READ_ERROR) {
                    cleanup(CancelReason::IOObjectShutdown);
                } else if (ret == READ_WOULDBLOCK) {
                    // nothing
                }
            } else {
                rio_.stop();
            }
        }
    }
    if (revent & ev::WRITE) {
        if (s_ == CONNECTING) {
            std::error_code ec;
            bool connected = socket_.is_connected(ec);
            auto &connect = getPending(IOObject::OpConnect);
            while (!connect.empty()) {
                auto p = static_cast<ConnectCompletionToken*>(&connect.front());
                p->ec = ec;
                p->notifyDone();
            }

            if (connected) {
                s_ = CONNECTED;
                handleInitialReadWrite();
            } else {
                cleanup(CancelReason::IOObjectShutdown);
                return;
            }
        }
        if (s_ == CONNECTED) {
            auto &writer = getPending(IOObject::OpWrite);
            while (!writer.empty()) {
                auto p = static_cast<WriterCompletionToken*>(&writer.front());
                std::error_code ec;
                iovec *vec;
                size_t vecLen = 0;
                p->prepareIov(&vec, &vecLen);
                if (!vecLen) {
                    p->notifyDone();
                    continue;
                }
                size_t countWritten;
                size_t partialWritten;
                ssize_t totalWritten = performWrite(vec, vecLen,
                        &countWritten, &partialWritten, ec);
                if (!ec) {
                    p->updateIov(totalWritten, countWritten, partialWritten);
                    if (countWritten == vecLen) {
                        p->notifyDone();
                        if (shutdown_flags_ & SHUT_WRITE_PENDING) {
                            shutdownWriteNow();
                            break;
                        }
                    } else {
                        break;
                    }
                } else {
                    p->writeError(ec);
                    cleanup(CancelReason::IOObjectShutdown);
                    break;
                }
            }
            if (writer.empty()) {
                wio_.stop();
                if (shutdown_flags_ & SHUT_WRITE_PENDING)
                    shutdownWriteNow();
            }
        }
    }

}

// future API
SockConnectFuture
SocketChannel::connect(EventExecutor *ev, const folly::SocketAddress &addr) {
    return SockConnectFuture(ev, addr);
}

SockWriteFuture
SocketChannel::write(std::unique_ptr<folly::IOBuf> buf) {
    return SockWriteFuture(shared_from_this(), std::move(buf));
}

SockReadStream
SocketChannel::readStream() {
    return SockReadStream(shared_from_this());
}

}
}

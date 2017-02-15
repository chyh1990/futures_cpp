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

ssize_t SocketChannel::performRead(SocketChannel::ReaderCompletionToken *tok, std::error_code &ec) {
    while (true) {
        void *buf;
        size_t bufLen = 0;
        tok->prepareBuffer(&buf, &bufLen);
        assert(buf);
        assert(bufLen > 0);
        ssize_t read_ret = doAsyncRead(buf, bufLen, ec);
        FUTURES_DLOG(INFO) << "readed: " << read_ret;
        if (read_ret == READ_ERROR) {
            tok->readError(ec);
        } else if (read_ret == READ_WOULDBLOCK) {
            tok->dataReady(0);
            return read_ret;
        } else if (read_ret == READ_EOF) {
            FUTURES_DLOG(INFO) << "Socket EOF";
            tok->readEof();
            return read_ret;
        } else {
            tok->dataReady(read_ret);
            if (read_ret < bufLen) {
                return read_ret;
            }
        }
    }
}

}
}

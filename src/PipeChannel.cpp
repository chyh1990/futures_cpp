#include <futures/io/PipeChannel.h>

namespace futures {
namespace io {

PipeChannel::PipeChannel(EventExecutor *ev, folly::File rfd, folly::File wfd)
    : Channel(ev), rfd_(std::move(rfd)), wfd_(std::move(wfd)),
      rio_(ev->getLoop()), wio_(ev->getLoop())
{
    if (!rfd_ && !wfd_) throw IOError("Invalid pipe");
    if (rfd_) {
        rio_.set<PipeChannel, &PipeChannel::onEvent>(this);
        rio_.set(rfd_.fd(), ev::READ);
    }

    if (wfd_) {
        wio_.set<PipeChannel, &PipeChannel::onEvent>(this);
        wio_.set(wfd_.fd(), ev::WRITE);
    }
}

io::intrusive_ptr<ReaderCompletionToken> PipeChannel::doRead(std::unique_ptr<ReaderCompletionToken> p)
{
    if (!rfd_) throw IOError("Write-only pipe.");
    if (!getPending(IOObject::OpRead).empty())
        throw IOError("Already reading");
    if (!good())
        throw IOError("Pipe closed");
    io::intrusive_ptr<ReaderCompletionToken> tok(p.release());
    tok->attach(this);
    rio_.start();
    return tok;
}

io::intrusive_ptr<WriterCompletionToken> PipeChannel::doWrite(std::unique_ptr<WriterCompletionToken> p)
{
    if (!wfd_) throw IOError("Read-only pipe.");
    io::intrusive_ptr<WriterCompletionToken> tok(p.release());
    tok->attach(this);
    wio_.start();
    return tok;
}

ssize_t PipeChannel::performWrite(
        const iovec* vec,
        size_t count,
        size_t* countWritten,
        size_t* partialWritten,
        std::error_code &ec)
{
    throw std::runtime_error("unimpl");
}

void PipeChannel::onEvent(ev::io& watcher, int revent) {
    if (wfd_ && (revent & ev::WRITE)) {
        if (s_ == CLOSED) {
            failAllWrites();
            return;
        }
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
#if 0
                    if (shutdown_flags_ & SHUT_WRITE_PENDING) {
                        shutdownWriteNow();
                        break;
                    }
#endif
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
#if 0
            if (shutdown_flags_ & SHUT_WRITE_PENDING)
                shutdownWriteNow();
#endif
        }
    }
    if (rfd_ && (revent & ev::READ)) {
        auto &reader = getPending(IOObject::OpRead);
        if (!reader.empty()) {
            auto first = static_cast<ReaderCompletionToken*>(&reader.front());
            std::error_code ec;
            ssize_t ret = handleRead(first, ec);
            if (ret == READ_EOF) {
                rio_.stop();
            } else if (ret == READ_ERROR) {
                cleanup(CancelReason::IOObjectShutdown);
            } else if (ret == READ_WOULDBLOCK) {
                // skip
            }
        } else {
            rio_.stop();
        }
    }
}

ReadStream PipeChannel::readStream() {
    return ReadStream(shared_from_this());
}

WriteFuture PipeChannel::write(std::unique_ptr<folly::IOBuf> buf)
{
    return WriteFuture(shared_from_this(), std::move(buf));
}

ssize_t PipeChannel::performRead(void* buf, size_t buflen, std::error_code &ec) {
again:
    ssize_t r = ::read(rfd_.fd(), buf, buflen);
    if (r > 0) {
        return r;
    } else if (r == 0) {
        return READ_EOF;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return READ_WOULDBLOCK;
    } else if (errno == EINTR) {
        goto again;
    } else {
        ec = std::error_code(errno, std::system_category());
        return READ_ERROR;
    }
}

ssize_t PipeChannel::handleRead(ReaderCompletionToken *tok, std::error_code &ec) {
    const static size_t kMaxReadPerEvent = 12;
    size_t reads = 0;
    while (reads < kMaxReadPerEvent) {
        void *buf;
        size_t bufLen = 0;
        tok->prepareBuffer(&buf, &bufLen);
        ssize_t read_ret = performRead(buf, bufLen, ec);
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

}
}


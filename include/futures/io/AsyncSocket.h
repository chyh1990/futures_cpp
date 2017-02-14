#pragma once

#include <futures/TcpStream.h>
#include <futures/io/WaitHandleBase.h>
#include <futures/core/SocketAddress.h>

namespace futures {
namespace io {

class SocketChannel : public IOObject {
    enum State {
        INITED,
        CONNECTING,
        CONNECTED,
        CLOSED,
    };

    enum {
        READ_EOF = 0,
        READ_WOULDBLOCK = -1,
        READ_ERROR = -2,
    };
public:
    using Ptr = std::shared_ptr<SocketChannel>;

    SocketChannel(EventExecutor *ev)
        : IOObject(ev), rio_(ev->getLoop()), wio_(ev->getLoop())
    {
        rio_.set<SocketChannel, &SocketChannel::onEvent>(this);
        wio_.set<SocketChannel, &SocketChannel::onEvent>(this);
    }

    struct ReaderCompletionToken : public io::CompletionToken {
        std::error_code ec;
        std::unique_ptr<folly::IOBuf> buf_;
        using Item = std::unique_ptr<folly::IOBuf>;

        ReaderCompletionToken(SocketChannel *ctx)
            : io::CompletionToken(ctx, IOObject::OpRead) {
        }

        void onCancel(CancelReason r) override {
        }

        ssize_t doRead(std::error_code &ec) {
            while (true) {
                if (!buf_) {
                    buf_ = folly::IOBuf::create(4096);
                }
                if (!buf_->prev()->tailroom())
                    buf_->prev()->appendChain(folly::IOBuf::create(4096));
                folly::IOBuf *last = buf_->prev();
                size_t to_read = last->tailroom();
                auto s = static_cast<SocketChannel*>(getIOObject());
                ssize_t read_ret = s->performRead(last->writableTail(), last->tailroom(), ec);
                FUTURES_DLOG(INFO) << "readed: " << read_ret;
                if (read_ret == READ_ERROR) {
                    this->ec = ec;
                    notifyDone();
                } else if (read_ret == READ_WOULDBLOCK) {
                    notify();
                    return read_ret;
                } else if (read_ret == READ_EOF) {
                    FUTURES_DLOG(INFO) << "Socket EOF";
                    notifyDone();
                    return read_ret;
                } else {
                    last->append(read_ret);
                    if (read_ret < to_read) {
                        notify();
                        return read_ret;
                    }
                }
            }
        }

        Poll<Optional<Item>> pollStream() {
            switch (getState()) {
            case STARTED:
                if (!buf_ || buf_->empty()) {
                    park();
                    return Poll<Optional<Item>>(not_ready);
                } else {
                    return makePollReady(Optional<Item>(std::move(buf_)));
                }
            case DONE:
                if (buf_ && !buf_->empty()) {
                    return makePollReady(Optional<Item>(std::move(buf_)));
                }
                if (ec) {
                    return Poll<Optional<Item>>(IOError("recv", ec));
                } else {
                    return makePollReady(Optional<Item>());
                }
            case CANCELLED:
                return Poll<Optional<Item>>(FutureCancelledException());
            }
        }
    };

    struct WriterCompletionToken : public io::CompletionToken {
        std::error_code ec;
        ssize_t written = 0;

        WriterCompletionToken(SocketChannel *ctx, std::unique_ptr<folly::IOBuf> buf)
            : io::CompletionToken(ctx, IOObject::OpWrite), buf_(std::move(buf)) {
            size_t chain_len = buf_->countChainElements();
            if (!chain_len)
                throw std::invalid_argument("empty chain");
            if (chain_len <= kMaxIovLen) {
                iovec_len_ = buf_->fillIov(small_vec_, kMaxIovLen);
                piovec_ = small_vec_;
            } else {
                vec_ = buf_->getIov();
                piovec_ = vec_.data();
                iovec_len_ = vec_.size();
            }
        }

        bool doWrite() {
            auto s = static_cast<SocketChannel*>(getIOObject());
            size_t countWritten;
            size_t partialWritten;
            std::error_code ec;
            ssize_t totalWritten = s->performWrite(piovec_, iovec_len_,
                    &countWritten, &partialWritten, ec);
            if (ec) {
                this->ec = ec;
                return true;
            } else {
                written += totalWritten;
                if (countWritten == iovec_len_) {
                    // done
                    return true;
                } else {
                    piovec_ += countWritten;
                    iovec_len_ -= countWritten;
                    assert(iovec_len_ > 0);
                    piovec_[0].iov_base = reinterpret_cast<char*>(piovec_[0].iov_base) + partialWritten;
                    piovec_[0].iov_len -= partialWritten;
                    return false;
                }
            }
        }

        void onCancel(CancelReason r) override {
        }

        Poll<ssize_t> poll() {
            switch (getState()) {
            case STARTED:
                park();
                return Poll<ssize_t>(not_ready);
            case DONE:
                if (ec)
                    return Poll<ssize_t>(IOError("writev", ec));
                else
                    return makePollReady(written);
            case CANCELLED:
                return Poll<ssize_t>(FutureCancelledException());
            }
        }

    protected:
        ~WriterCompletionToken() {
            cleanup(CancelReason::UserCancel);
        }

        std::unique_ptr<folly::IOBuf> buf_;

        static const size_t kMaxIovLen = 32;
        iovec small_vec_[kMaxIovLen];
        std::vector<iovec> vec_;

        iovec *piovec_ = nullptr;
        size_t iovec_len_ = 0;
    };

    struct ConnectCompletionToken : public io::CompletionToken {
        std::error_code ec;

        ConnectCompletionToken(SocketChannel *ctx)
            : io::CompletionToken(ctx, IOObject::OpConnect)
        {
        }

        void onCancel(CancelReason r) override {
        }

        Poll<folly::Unit> poll() {
            switch (getState()) {
            case STARTED:
                park();
                return Poll<folly::Unit>(not_ready);
            case DONE:
                if (ec)
                    return Poll<folly::Unit>(IOError("connect", ec));
                else
                    return makePollReady(folly::unit);
            case CANCELLED:
                return Poll<folly::Unit>(FutureCancelledException());
            }
        }

        ~ConnectCompletionToken() {
            cleanup(CancelReason::UserCancel);
        }
    };

    io::intrusive_ptr<ConnectCompletionToken> doConnect(const folly::SocketAddress &addr) {
        if (s_ != INITED)
            throw IOError("Already connecting");
        peer_addr_ = addr;
        io::intrusive_ptr<ConnectCompletionToken> tok(new ConnectCompletionToken(this));
        if (startConnect(tok->ec)) {
            tok->notifyDone();
        }
        return tok;
    }

    io::intrusive_ptr<ReaderCompletionToken> doRead() {
        if (!getPending(IOObject::OpRead).empty())
            throw IOError("Already reading");
        if (s_ == INITED)
            throw IOError("Not connecting");
        io::intrusive_ptr<ReaderCompletionToken> tok(new ReaderCompletionToken(this));
        if (s_ == CLOSED) {
            tok->ec = std::make_error_code(std::errc::connection_aborted);
            tok->notifyDone();
        } else {
            rio_.start();
        }
        return tok;
    }

    io::intrusive_ptr<WriterCompletionToken> doWrite(std::unique_ptr<folly::IOBuf> buf) {
        if (s_ == INITED)
            throw IOError("Not connecting");
        io::intrusive_ptr<WriterCompletionToken> tok(new WriterCompletionToken(this, std::move(buf)));
        if (s_ == CLOSED) {
            tok->ec = std::make_error_code(std::errc::connection_aborted);
            tok->notifyDone();
        } else if (s_ == CONNECTED) {
            // TODO try send immediately
            wio_.start();
        }
        return tok;
    }
    io::intrusive_ptr<WriterCompletionToken> doFlush();

    bool startConnect(std::error_code &ec);

    ssize_t performWrite(
            const iovec* vec,
            size_t count,
            size_t* countWritten,
            size_t* partialWritten,
            std::error_code &ec);

    ssize_t performRead(void* buf, size_t buflen, std::error_code &ec) {
        ssize_t r = socket_.recv(buf, buflen, 0, ec);
        if (!ec) {
            return r == 0 ? READ_EOF : r;
        } else if (ec == std::make_error_code(std::errc::operation_would_block)) {
            return READ_WOULDBLOCK;
        } else {
            return READ_ERROR;
        }
    }

private:
    tcp::Socket socket_;
    folly::SocketAddress peer_addr_;
    State s_ = INITED;
    ev::io rio_;
    ev::io wio_;

    void onEvent(ev::io& watcher, int revent) {
        if (revent & ev::ERROR)
            throw std::runtime_error("syscall error");
        if (revent & ev::READ) {
            if (s_ == CONNECTED) {
                auto &reader = getPending(IOObject::OpRead);
                if (!reader.empty()) {
                    auto first = static_cast<ReaderCompletionToken*>(&reader.front());
                    std::error_code ec;
                    ssize_t ret = first->doRead(ec);
                    if (ret == READ_EOF) {
                        // todo
                        closeRead();
                    } else if (ret == READ_ERROR) {
                        forceClose();
                    } else if (ret == READ_WOULDBLOCK) {
                        // nothing
                    }
                }
                if (reader.empty())
                    rio_.stop();
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
                } else {
                    forceClose();
                    return;
                }
            }
            if (s_ == CONNECTED) {
                auto &writer = getPending(IOObject::OpWrite);
                while (!writer.empty()) {
                    auto p = static_cast<WriterCompletionToken*>(&writer.front());
                    if (p->doWrite()) {
                        p->notifyDone();
                    } else {
                        break;
                    }
                }
                if (writer.empty())
                    wio_.stop();
            }
        }
    }

    void closeRead() {
        rio_.stop();
    }

    void forceClose() {
        wio_.stop();
        rio_.stop();
        socket_.close();
        cleanup(CancelReason::IOObjectShutdown);
        s_ = CLOSED;
    }
};

class ConnectFuture : public FutureBase<ConnectFuture, folly::Unit> {
public:
    using Item = folly::Unit;

    ConnectFuture(SocketChannel::Ptr ptr, const folly::SocketAddress &addr)
        : ptr_(ptr), addr_(addr) {}

    Poll<Item> poll() override {
        if (!tok_)
            tok_ = ptr_->doConnect(addr_);
        return tok_->poll();
    }
private:
    SocketChannel::Ptr ptr_;
    folly::SocketAddress addr_;
    io::intrusive_ptr<SocketChannel::ConnectCompletionToken> tok_;
};

class SockWriteFuture : public FutureBase<SockWriteFuture, ssize_t> {
public:
    using Item = ssize_t;

    SockWriteFuture(SocketChannel::Ptr ptr, std::unique_ptr<folly::IOBuf> buf)
        : ptr_(ptr), buf_(std::move(buf)) {}

    Poll<Item> poll() override {
        if (!tok_)
            tok_ = ptr_->doWrite(std::move(buf_));
        return tok_->poll();
    }
private:
    SocketChannel::Ptr ptr_;
    std::unique_ptr<folly::IOBuf> buf_;
    io::intrusive_ptr<SocketChannel::WriterCompletionToken> tok_;
};

class SockReadStream : public StreamBase<SockReadStream, std::unique_ptr<folly::IOBuf>> {
public:
    using Item = std::unique_ptr<folly::IOBuf>;

    SockReadStream(SocketChannel::Ptr ptr)
        : ptr_(ptr) {}

    Poll<Optional<Item>> poll() override {
        if (!tok_)
            tok_ = ptr_->doRead();
        return tok_->pollStream();
    }
private:
    SocketChannel::Ptr ptr_;
    io::intrusive_ptr<SocketChannel::ReaderCompletionToken> tok_;
};



}
}

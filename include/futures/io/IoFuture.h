#pragma once

#include <futures/io/Io.h>
#include <futures/core/IOBufQueue.h>
#include <futures/Future.h>
#include <futures/Stream.h>
#include <futures/AsyncSink.h>
#include <futures/EventExecutor.h>

namespace futures {
namespace io {

template <typename Decoder>
class FramedStream :
    public StreamBase<FramedStream<Decoder>, typename Decoder::Out> {
public:
    using Item = typename Decoder::Out;

    const static size_t kRdBufSize = 8 * 1024;

    Poll<Optional<Item>> poll() override {
        while (true) {
            if (readable_) {
                if (eof_) {
                    if (q_.empty())
                        return makePollReady(Optional<Item>());
                    auto f = codec_.decode_eof(q_);
                    if (f.hasException())
                        return Poll<Optional<Item>>(f.exception());
                    return makePollReady(Optional<Item>(folly::moveFromTry(f)));
                }
                // FUTURES_DLOG(INFO) << "RDBUF: " << rdbuf_->length();
                if (q_.empty()) {
                    readable_ = false;
                } else {
                    auto f = codec_.decode(q_);
                    if (f.hasException())
                        return Poll<Optional<Item>>(f.exception());
                    if (f->hasValue()) {
                        return makePollReady(folly::moveFromTry(f));
                    } else {
                        readable_ = false;
                    }
                }
            }
            assert(!eof_);
            if (!q_.tailroom())
                q_.preallocate(32, 4000);
            assert(q_.tailroom() > 0);
            std::error_code ec;
            ssize_t len = io_->read(q_.writableTail(), q_.tailroom(), ec);
            if (ec == std::make_error_code(std::errc::connection_aborted)) {
                assert(len == 0);
                eof_ = true;
                readable_ = true;
            } else if (ec) {
                io_.reset();
                return Poll<Optional<Item>>(IOError("read frame", ec));
            } else if (len == 0) {
                if (io_->poll_read().isReady()) {
                    continue;
                } else {
                    return Poll<Optional<Item>>(not_ready);
                }
            } else {
                assert(len > 0);
                q_.postallocate(len);
                FUTURES_DLOG(INFO) << "HERE: " << q_.chainLength();
                readable_ = true;
            }
        }
    }

    FramedStream(std::unique_ptr<Io> io)
        : io_(std::move(io)), eof_(false), readable_(false),
            q_(folly::IOBufQueue::cacheChainLength()) {
    }
private:
    std::unique_ptr<Io> io_;
    Decoder codec_;
    bool eof_;
    bool readable_;
    folly::IOBufQueue q_;
};

template <typename Encoder>
class FramedSink :
    public AsyncSinkBase<FramedSink<Encoder>, typename Encoder::Out> {
public:
    using Out = typename Encoder::Out;

    FramedSink(std::unique_ptr<Io> io)
        : io_(std::move(io)) {
    }

    Try<void> startSend(Out& item) override {
        return codec_.encode(item, q_);
    }

    Poll<folly::Unit> pollComplete() override {
        const size_t kMaxIovLen = 64;
        while (!q_.empty()) {
            size_t chain_len = q_.front()->countChainElements();
            FUTURES_DLOG(INFO) << "flushing frame " << chain_len;
            size_t veclen;
            std::error_code ec;
            ssize_t sent;
            size_t countWritten, partialBytes;
            if (chain_len <= kMaxIovLen) {
                iovec vec[kMaxIovLen];
                veclen = q_.front()->fillIov(vec, kMaxIovLen);
                sent = performWrite(vec, veclen, &countWritten, &partialBytes, ec);
            } else {
                iovec *vec = new iovec[chain_len];
                veclen = q_.front()->fillIov(vec, chain_len);
                sent = performWrite(vec, veclen, &countWritten, &partialBytes, ec);
                delete [] vec;
            }
            if (ec) {
                io_.reset();
                return Poll<folly::Unit>(IOError("pollComplete", ec));
            } else {
                assert(sent >= 0);
                if (sent > 0)
                    q_.trimStart(sent);
                if (countWritten < veclen) {
                    if (!io_->poll_write().isReady())
                        return Poll<folly::Unit>(not_ready);
                }
            }
        }
        return makePollReady(folly::Unit());
    }

private:
    std::unique_ptr<Io> io_;
    Encoder codec_;
    folly::IOBufQueue q_;

    ssize_t performWrite(
            const iovec* vec,
            size_t count,
            size_t* countWritten,
            size_t* partialWritten,
            std::error_code &ec) {
        ssize_t totalWritten = io_->writev(vec, count, ec);
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

};

class DescriptorIo : public io::Io, public EventWatcherBase {
public:
    DescriptorIo(EventExecutor* reactor, int fd)
        : reactor_(reactor), io_(reactor->getLoop()),
        fd_(fd) {
        assert(reactor_);
        if (fd < 0) throw IOError("invalid fd");
        // reactor_->linkWatcher(this);
        io_.set<DescriptorIo, &DescriptorIo::onEvent>(this);
    }

    Async<folly::Unit> poll_read() override {
        if(!event_hook_.is_linked()) {
            reactor_->linkWatcher(this);
            task_ = CurrentTask::park();
            io_.start(fd_, ev::READ);
        }
        return not_ready;
    }

    Async<folly::Unit> poll_write() override {
        if(!event_hook_.is_linked()) {
            reactor_->linkWatcher(this);
            task_ = CurrentTask::park();
            io_.start(fd_, ev::WRITE);
        }
        return not_ready;
    }

    void cleanup(int reason) override {
        io_.stop();
        if (task_) task_->unpark();
    }

    virtual ~DescriptorIo() {
        clear();
    }

private:
    EventExecutor* reactor_;
    ev::io io_;
    int fd_;
    Optional<Task> task_;

    void onEvent(ev::io &watcher, int revent) {
        if (revent & ev::ERROR)
            throw EventException("syscall error");
        FUTURES_DLOG(INFO) << "EVENT: " << fd_ << " " << task_.hasValue();
        if (task_) task_->unpark();
        clear();
    }

    void clear() {
        io_.stop();
        if (event_hook_.is_linked())
            reactor_->unlinkWatcher(this);
        task_.clear();
    }

};

class WriteFuture : public FutureBase<WriteFuture, ssize_t> {
public:
    enum State {
        INIT,
        SENT,
        CANCELLED,
    };
    using Item = ssize_t;

    WriteFuture(std::unique_ptr<Io> io,
            std::unique_ptr<folly::IOBuf> buf)
        : io_(std::move(io)), s_(INIT),
        buf_(std::move(buf)) {}

    Poll<Item> poll() override {
    retry:
        std::error_code ec;
        switch (s_) {
            case INIT: {
                // register event
                ssize_t len = io_->write(buf_->data(), buf_->length(), ec);
                if (ec) {
                    io_.reset();
                    return Poll<Item>(IOError("send", ec));
                } else if (len < buf_->length()) {
                    assert(len >= 0);
                    buf_->trimStart(len);
                    if (io_->poll_write().isNotReady()) {
                        s_ = INIT;
                    } else {
                        goto retry;
                    }
                } else {
                    s_ = SENT;
                    io_.reset();
                    return makePollReady(len);
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

    void cancel() override {
        io_.reset();
        s_ = CANCELLED;
    }
private:
    std::unique_ptr<Io> io_;
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

template <typename ReadPolicy>
class ReadFuture : public FutureBase<ReadFuture<ReadPolicy>,
    std::unique_ptr<folly::IOBuf>>
{
public:
    enum State {
        INIT,
        DONE,
        CANCELLED,
    };
    using Item = std::unique_ptr<folly::IOBuf>;

    ReadFuture(std::unique_ptr<Io> io, const ReadPolicy &policy)
        : policy_(policy), io_(std::move(io)),
        buf_(folly::IOBuf::create(policy_.bufferSize())) {
    }

    Poll<Item> poll() override {
    retry:
        switch (s_) {
            case INIT: {
                // register event
                std::error_code ec;
                ssize_t len = io_->read(buf_.get(), policy_.remainBufferSize(), ec);
                FUTURES_DLOG(INFO) << "S " << "LEN " << len;
                if (ec) {
                    return Poll<Item>(IOError("recv", ec));
                } else if (len == 0) {
                    s_ = INIT;
                } else {
                    buf_->append(len);
                    if (policy_.read(len)) {
                        s_ = DONE;
                        io_.reset();
                        return Poll<Item>(Async<Item>(std::move(buf_)));
                    }
                }
                if (io_->poll_read().isNotReady()) {
                    break;
                } else {
                    goto retry;
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

    void cancel() override {
        io_.reset();
        s_ = CANCELLED;
    }
private:
    State s_ = INIT;
    ReadPolicy policy_;
    std::unique_ptr<Io> io_;
    std::unique_ptr<folly::IOBuf> buf_;
};

}
}

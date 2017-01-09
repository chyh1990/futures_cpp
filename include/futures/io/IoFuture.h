#pragma once

#include <futures/io/Io.h>
#include <futures/Future.h>
#include <futures/EventExecutor.h>

namespace futures {
namespace io {

class DescriptorIo : public io::Io, public EventWatcherBase {
public:
    DescriptorIo(EventExecutor* reactor, int fd)
        : reactor_(reactor), io_(reactor->getLoop()), fd_(fd) {
        assert(reactor_);
        assert(fd >= 0);
        // reactor_->linkWatcher(this);
        io_.set<DescriptorIo, &DescriptorIo::onEvent>(this);
    }

    Async<folly::Unit> poll_read() override {
        assert(!event_hook_.is_linked());
        reactor_->linkWatcher(this);
        task_ = CurrentTask::park();
        io_.start(fd_, ev::READ);
        return not_ready;
    }

    Async<folly::Unit> poll_write() override {
        assert(!event_hook_.is_linked());
        reactor_->linkWatcher(this);
        task_ = CurrentTask::park();
        io_.start(fd_, ev::WRITE);
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
        if (event_hook_.is_linked())
            reactor_->unlinkWatcher(this);
        if (task_) task_->unpark();
        task_.clear();
    }

    void clear() {
        io_.stop();
        if (event_hook_.is_linked())
            reactor_->unlinkWatcher(this);
        task_.clear();
    }

};

class SendFuture : public FutureBase<SendFuture, ssize_t> {
public:
    enum State {
        INIT,
        SENT,
        CANCELLED,
    };
    using Item = ssize_t;

    SendFuture(std::unique_ptr<Io> io,
            std::unique_ptr<folly::IOBuf> buf)
        : io_(std::move(io)), s_(INIT),
        buf_(std::move(buf)) {}

    Poll<Item> poll() override {
    retry:
        std::error_code ec;
        switch (s_) {
            case INIT: {
                // register event
                ssize_t len = io_->write(*buf_.get(), buf_->length(), ec);
                if (ec) {
                    io_.reset();
                    return Poll<Item>(IOError("send", ec));
                } else if (len == 0) {
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
class RecvFuture : public FutureBase<RecvFuture<ReadPolicy>,
    std::unique_ptr<folly::IOBuf>>
{
public:
    enum State {
        INIT,
        DONE,
        CANCELLED,
    };
    using Item = std::unique_ptr<folly::IOBuf>;

    RecvFuture(std::unique_ptr<Io> io, const ReadPolicy &policy)
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
                    if (io_->poll_read().isNotReady()) {
                        break;
                    } else {
                        goto retry;
                    }
                } else {
                    buf_->append(len);
                    if (policy_.read(len)) {
                        s_ = DONE;
                        io_.reset();
                        return Poll<Item>(Async<Item>(std::move(buf_)));
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

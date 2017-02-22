#pragma once

#include <futures/io/WaitHandleBase.h>
#include <futures/core/IOBuf.h>

namespace futures {
namespace io {

class ReaderCompletionToken : public io::CompletionToken {
public:
    ReaderCompletionToken()
        : io::CompletionToken(IOObject::OpRead) {
    }

    void onCancel(CancelReason r) override {
    }

    virtual void readEof() {
        notifyDone();
    }

    virtual void readError(std::error_code ec) {
        ec_ = ec;
        notifyDone();
    }

    virtual void dataReady(ssize_t size) = 0;
    virtual void prepareBuffer(void **buf, size_t *data) = 0;

    ~ReaderCompletionToken() {
        cleanup(CancelReason::UserCancel);
    }

    std::error_code getErrorCode() const {
        return ec_;
    }

private:
    std::error_code ec_;
};

class WriterCompletionToken : public io::CompletionToken {
public:
    WriterCompletionToken(std::unique_ptr<folly::IOBuf> buf)
        : io::CompletionToken(IOObject::OpWrite), buf_(std::move(buf)) {
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

    virtual void writeError(std::error_code ec) {
        ec_ = ec;
        notifyDone();
    }

    virtual void prepareIov(struct iovec **vec, size_t *vecLen) {
        *vecLen = iovec_len_;
        *vec = piovec_;
    }

    virtual void updateIov(ssize_t totalWritten, size_t countWritten, size_t partialWritten) {
        written_ += totalWritten;
        piovec_ += countWritten;
        iovec_len_ -= countWritten;
        if (iovec_len_ > 0) {
            piovec_[0].iov_base = reinterpret_cast<char*>(piovec_[0].iov_base) + partialWritten;
            piovec_[0].iov_len -= partialWritten;
        }
    }

    void onCancel(CancelReason r) override {
    }

    virtual Poll<ssize_t> poll() {
        switch (getState()) {
        case STARTED:
            park();
            return Poll<ssize_t>(not_ready);
        case DONE:
            if (ec_)
                return Poll<ssize_t>(IOError("writev", ec_));
            else
                return makePollReady(written_);
        case CANCELLED:
            return Poll<ssize_t>(FutureCancelledException());
        }
    }

    std::error_code getErrorCode() const {
        return ec_;
    }

    ~WriterCompletionToken() {
        cleanup(CancelReason::UserCancel);
    }

private:
    std::unique_ptr<folly::IOBuf> buf_;
    std::error_code ec_;
    ssize_t written_ = 0;

    static const size_t kMaxIovLen = 32;
    struct iovec small_vec_[kMaxIovLen];
    std::vector<struct iovec> vec_;
    iovec *piovec_ = nullptr;
    size_t iovec_len_ = 0;
};


class Channel : public IOObject {
public:
    using Ptr = std::shared_ptr<Channel>;

    virtual void shutdownWrite() = 0;
    virtual void shutdownWriteNow() = 0;

    Channel(EventExecutor *ev) : IOObject(ev) {}
    virtual bool good() const { return true; }

    virtual io::intrusive_ptr<WriterCompletionToken> doWrite(std::unique_ptr<WriterCompletionToken> p) = 0;
    virtual io::intrusive_ptr<ReaderCompletionToken> doRead(std::unique_ptr<ReaderCompletionToken> p) = 0;
};

}
}

#pragma once

#include <futures/Future.h>
#include <futures/Stream.h>
#include <futures/io/AsyncSocket.h>

namespace futures {
namespace io {

class WriteFuture : public FutureBase<WriteFuture, ssize_t> {
public:
    using Item = ssize_t;

    WriteFuture(Channel::Ptr ptr, std::unique_ptr<folly::IOBuf> buf)
        : ptr_(ptr), buf_(std::move(buf)) {}

    Poll<Item> poll() override {
        if (!tok_)
            tok_ = ptr_->doWrite(folly::make_unique<WriterCompletionToken>(std::move(buf_)));
        return tok_->poll();
    }
private:
    Channel::Ptr ptr_;
    std::unique_ptr<folly::IOBuf> buf_;
    io::intrusive_ptr<WriterCompletionToken> tok_;
};

class ReadStream : public StreamBase<ReadStream, std::unique_ptr<folly::IOBuf>> {
public:
    using Item = std::unique_ptr<folly::IOBuf>;

    struct StreamCompletionToken : public ReaderCompletionToken {
    public:
        StreamCompletionToken() {}

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
                if (getErrorCode()) {
                    return Poll<Optional<Item>>(IOError("recv", getErrorCode()));
                } else {
                    return makePollReady(Optional<Item>());
                }
            case CANCELLED:
                return Poll<Optional<Item>>(FutureCancelledException());
            }
        }

        void prepareBuffer(void **buf, size_t *bufLen) override {
            if (!buf_) buf_ = folly::IOBuf::create(2048);
            auto last = buf_->prev();
            assert(last);
            if (last->tailroom() == 0) {
                last->appendChain(folly::IOBuf::create(2048));
                last = last->next();
            }
            *buf = last->writableTail();
            *bufLen = last->tailroom();
        }

        void dataReady(ssize_t size) override {
            buf_->prev()->append(size);
            notify();
        }
    private:
        std::unique_ptr<folly::IOBuf> buf_;
    };

    ReadStream(Channel::Ptr ptr)
        : ptr_(ptr) {}

    Poll<Optional<Item>> poll() override {
        if (!tok_)
            tok_ = ptr_->doRead(folly::make_unique<StreamCompletionToken>());
        return static_cast<StreamCompletionToken*>(tok_.get())->pollStream();
    }
private:
    Channel::Ptr ptr_;
    io::intrusive_ptr<ReaderCompletionToken> tok_;
};



}
}

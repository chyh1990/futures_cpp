#pragma once

#include <futures/Future.h>
#include <futures/Stream.h>
#include <futures/AsyncSink.h>
#include <futures/EventExecutor.h>
#include <futures/io/Channel.h>
#include <futures/core/IOBufQueue.h>

namespace futures {
namespace io {

template <typename Decoder>
class FramedStream :
    public StreamBase<FramedStream<Decoder>, typename Decoder::Out> {
public:
    using Item = typename Decoder::Out;

    const static size_t kRdBufSize = 8 * 1024;

    struct FramedStreamReader : public ReaderCompletionToken {
        FramedStreamReader()
            : eof_(false), readable_(false),
            q_(folly::IOBufQueue::cacheChainLength()) {
        }

        void readEof() {
            ReaderCompletionToken::readEof();
        }

        void prepareBuffer(void **ptr, size_t *len) override {
            auto buf = q_.preallocate(2000, kRdBufSize);
            *ptr = buf.first;
            *len = buf.second;
        }

        void dataReady(ssize_t size) override {
            q_.postallocate(size);
            if (size > 0) readable_ = true;
            notify();
        }

        Poll<Optional<Item>> pollStream() {
            switch (getState()) {
                case STARTED:
                case DONE:
                    return pollOneItem();
                case CANCELLED:
                    return Poll<Optional<Item>>(FutureCancelledException());
            }
        }

    private:
        folly::IOBufQueue q_;
        Decoder codec_;
        bool eof_;
        bool readable_;

        Poll<Optional<Item>> pollOneItem() {
            if (getErrorCode())
                return Poll<Optional<Item>>(IOError("read", getErrorCode()));
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
                assert(!readable_);
                if (getState() == STARTED) {
                    park();
                    return Poll<Optional<Item>>(not_ready);
                } else {
                    readable_ = true;
                    eof_ = true;
                }
            }
        }
    };

    FramedStream(Channel::Ptr io)
        : io_(io) {
    }

    Poll<Optional<Item>> poll() override {
        if (!tok_)
            tok_ = io_->doRead(folly::make_unique<FramedStreamReader>());
        return static_cast<FramedStreamReader*>(tok_.get())->pollStream();
    }
private:
    Channel::Ptr io_;
    intrusive_ptr<ReaderCompletionToken> tok_;
};

template <typename Encoder>
class FramedSink :
    public AsyncSinkBase<FramedSink<Encoder>, typename Encoder::Out> {
public:
    using Out = typename Encoder::Out;

    FramedSink(Channel::Ptr io)
        : io_(std::move(io)) {
    }

    Try<void> startSend(Out&& item) override {
        return codec_.encode(std::move(item), q_);
    }

    Poll<folly::Unit> pollComplete() override {
        if (!write_req_) {
            if (q_.empty()) return makePollReady(folly::unit);
            write_req_ = io_->doWrite(folly::make_unique<WriterCompletionToken>(q_.move()));
        }
        auto r = write_req_->poll();
        if (r.hasException())
            return Poll<folly::Unit>(r.exception());
        if (r->hasValue()) {
            write_req_.reset();
            return makePollReady(folly::unit);
        }
        return Poll<folly::Unit>(not_ready);
    }

private:
    Channel::Ptr io_;
    Encoder codec_;
    folly::IOBufQueue q_;

    intrusive_ptr<WriterCompletionToken> write_req_;
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

}
}

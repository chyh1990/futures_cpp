#pragma once

#include <futures/Future.h>
#include <futures/Stream.h>
#include <futures/AsyncSink.h>
#include <futures/EventExecutor.h>
#include <futures/io/Channel.h>
#include <futures/codec/Codec.h>
#include <futures/core/IOBufQueue.h>

namespace futures {
namespace io {

template <typename T>
class FramedStream :
    public StreamBase<FramedStream<T>, T> {
public:
    using Item = T;

    const static size_t kRdBufSize = 8 * 1024;

    struct FramedStreamReader : public ReaderCompletionToken {
        FramedStreamReader(std::shared_ptr<codec::DecoderBase<T>> codec)
            : eof_(false), readable_(false),
            q_(folly::IOBufQueue::cacheChainLength()), codec_(codec) {
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
        std::shared_ptr<codec::DecoderBase<T>> codec_;
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
                        try {
                            return makePollReady(Optional<Item>(codec_->decodeEof(q_)));
                        } catch (std::exception &e) {
                            return Poll<Optional<Item>>(
                                folly::exception_wrapper(std::current_exception(), e));
                        }
                    }
                    // FUTURES_DLOG(INFO) << "RDBUF: " << rdbuf_->length();
                    if (q_.empty()) {
                        readable_ = false;
                    } else {
                        try {
                            auto f = codec_->decode(q_);
                            if (f.hasValue()) {
                                return makePollReady(std::move(f));
                            } else {
                                readable_ = false;
                            }
                        } catch (std::exception &e) {
                            return Poll<Optional<Item>>(
                                folly::exception_wrapper(std::current_exception(), e));
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

    FramedStream(Channel::Ptr io, std::shared_ptr<codec::DecoderBase<T>> decoder)
        : io_(io), codec_(decoder) {
    }

    Poll<Optional<Item>> poll() override {
        if (!tok_)
            tok_ = io_->doRead(folly::make_unique<FramedStreamReader>(codec_));
        return static_cast<FramedStreamReader*>(tok_.get())->pollStream();
    }
private:
    Channel::Ptr io_;
    std::shared_ptr<codec::DecoderBase<T>> codec_;
    intrusive_ptr<ReaderCompletionToken> tok_;
};

template <typename T>
class FramedSink :
    public AsyncSinkBase<FramedSink<T>, T> {
public:
    using Out = T;

    FramedSink(Channel::Ptr io, std::shared_ptr<codec::EncoderBase<T>> encoder)
        : io_(std::move(io)), codec_(encoder) {
    }

    Try<void> startSend(Out&& item) override {
        try {
            codec_->encode(std::move(item), q_);
            return Try<void>();
        } catch (std::exception &e) {
            return Try<void>(folly::exception_wrapper(std::current_exception(), e));
        }
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
    std::shared_ptr<codec::EncoderBase<T>> codec_;
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

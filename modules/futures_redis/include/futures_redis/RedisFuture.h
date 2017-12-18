#pragma once

#include <deque>
#include <futures/Future.h>
#include <futures/Stream.h>
#include <futures/EventExecutor.h>
#include <futures/io/WaitHandleBase.h>
#include <futures_redis/RedisReply.h>

struct redisAsyncContext;
struct redisReply;

namespace futures {

namespace redis_io {

class RedisException : public std::runtime_error {
public:
    RedisException(const std::string &err)
        : std::runtime_error(err) {}
};

class RedisCommandFuture;
class RedisCommandStream;

class AsyncContext : public io::IOObject, public std::enable_shared_from_this<AsyncContext> {
public:
    using Ptr = std::shared_ptr<AsyncContext>;

    AsyncContext(EventExecutor *loop, const std::string& addr, uint16_t port);
    ~AsyncContext();

    AsyncContext &operator=(const AsyncContext&) = delete;
    AsyncContext(const AsyncContext&) = delete;

    struct CompletionToken : public io::CompletionToken {
        const bool subscribe_;
        Try<Reply> reply_;
        std::deque<Try<Reply>> stream_;

        CompletionToken(bool subscribe)
            : io::CompletionToken(IOObject::OpWrite), subscribe_(subscribe) {}

        void onCancel(CancelReason r) override {
            // cannot cancel
        }

        Poll<Reply> poll() {
            assert(!subscribe_);
            switch (getState()) {
            case STARTED:
                park();
                return Poll<Reply>(not_ready);
            case DONE:
                return makePollReady(std::move(reply_));
            case CANCELLED:
                return Poll<Reply>(FutureCancelledException());
            }
        }

        Poll<Optional<Reply>> pollStream() {
            assert(subscribe_);
            switch (getState()) {
            case STARTED:
                break;
            case DONE:
                return makePollReady(Optional<Reply>());
            case CANCELLED:
                return Poll<Optional<Reply>>(FutureCancelledException());
            }
            if (!stream_.empty()) {
                Try<Reply> r(std::move(stream_.front()));
                stream_.pop_front();
                if (r.hasException()) {
                    return Poll<Optional<Reply>>(r.exception());
                } else {
                    return makePollReady(folly::make_optional(folly::moveFromTry(r)));
                }
            } else {
                park();
                return Poll<Optional<Reply>>(not_ready);
            }
        }

    protected:
        ~CompletionToken() {
        }
    };

    io::intrusive_ptr<CompletionToken>
        asyncFormattedCommand(const char *cmd, size_t len, bool subscribe);

    // int unsubsribe();

    bool isConnected() const {
        return connected_;
    }

    void onCancel(CancelReason r) override;

    // future API
    template <typename... Args>
    RedisCommandFuture execute(Args&&... args);

    template <typename... Args>
    RedisCommandStream subscribe(Args&&... args);
private:
    redisAsyncContext *c_;
    std::string addr_;
    uint16_t port_;

    bool reading_;
    bool writing_;
    bool connected_;
    ev::io rev_;
    ev::io wev_;

    static void redisAddRead(void *data);
    static void redisDelRead(void *data);
    static void redisAddWrite(void *data);
    static void redisDelWrite(void *data);
    static void redisCleanup(void *data);

    static void redisConnect(const redisAsyncContext *c, int status);
    static void redisDisconnect(const redisAsyncContext *c, int status);
    static void redisCallback(struct redisAsyncContext *, void *r, void *priv);

    void redisReadEvent(ev::io &watcher, int revent);
    void redisWriteEvent(ev::io &watcher, int revent);

    void reconnect();
    void reconnectIfNeeded() {
        if (!connected_ || !c_) reconnect();
    }
};


namespace detail {

class RawString {
public:
    RawString(char *ptr, size_t len)
        : ptr_(ptr), len_(len) {}

    RawString() : ptr_(nullptr), len_(0) {}

    ~RawString() { reset(); }
    const char *data() const { return ptr_; }
    size_t size() const { return len_; }

    RawString(RawString&& o)
        : ptr_(o.ptr_), len_(o.len_) {
        o.ptr_ = nullptr;
        o.len_ = 0;
    }

    RawString& operator=(RawString&& o) {
        if (this == &o)
            return *this;
        reset();
        ptr_ = o.ptr_;
        len_ = o.len_;
        o.ptr_ = nullptr;
        o.len_ = 0;
        return *this;
    }

    void reset() {
        if (ptr_) free(ptr_);
        ptr_ = nullptr;
        len_ = 0;
    }

    void assign(char *data, size_t len) {
        reset();
        ptr_ = data;
        len_ = len;
    }

    RawString(const RawString&) = delete;
    RawString& operator=(const RawString&) = delete;
private:
    char *ptr_;
    size_t len_;
};

}

class RedisCommandFuture : public FutureBase<RedisCommandFuture, Reply> {
public:
    using Item = Reply;
    RedisCommandFuture(AsyncContext::Ptr ctx, const char *format, ...);

    Poll<Item> poll() {
        if (!tok_.get())
            tok_ = ctx_->asyncFormattedCommand(cmd_.data(), cmd_.size(), false);
        return tok_->poll();
    }
private:
    AsyncContext::Ptr ctx_;
    detail::RawString cmd_;
    io::intrusive_ptr<AsyncContext::CompletionToken> tok_;
};

class RedisCommandStream : public StreamBase<RedisCommandStream, Reply> {
public:
    using Item = Reply;
    RedisCommandStream(AsyncContext::Ptr ctx, const char *format, ...);

    Poll<Optional<Item>> poll() override {
        if (!tok_.get())
            tok_ = ctx_->asyncFormattedCommand(cmd_.data(), cmd_.size(), true);
        return tok_->pollStream();
    }
private:
    AsyncContext::Ptr ctx_;
    detail::RawString cmd_;
    io::intrusive_ptr<AsyncContext::CompletionToken> tok_;
};


template <typename... Args>
RedisCommandFuture AsyncContext::execute(Args&&... args) {
    return RedisCommandFuture(shared_from_this(), std::forward<Args>(args)...);
}

template <typename... Args>
RedisCommandStream AsyncContext::subscribe(Args&&... args) {
    return RedisCommandStream(shared_from_this(), std::forward<Args>(args)...);
}

}

}

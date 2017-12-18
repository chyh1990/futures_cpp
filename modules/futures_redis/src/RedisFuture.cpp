#include <futures_redis/RedisFuture.h>
#include "hiredis/async.h"
#include "hiredis/hiredis.h"

namespace futures {
namespace redis_io {

AsyncContext::AsyncContext(EventExecutor *loop,
    const std::string& addr, uint16_t port)
    : io::IOObject(loop), c_(nullptr),
    addr_(addr), port_(port),
    rev_(loop->getLoop()), wev_(loop->getLoop()) {
    connected_ = false;
    // reconnect();
}

void AsyncContext::reconnect() {
    rev_.stop();
    wev_.stop();
    if (c_) {
            redisAsyncDisconnect(c_);
            c_ = nullptr;
    }
    FUTURES_DLOG(INFO) << "reconnecting to redis";
    c_ = redisAsyncConnect(addr_.c_str(), port_);
    if (!c_) throw RedisException("redisAsyncContext");
    if (c_->err) {
            std::string err(c_->errstr);
            redisAsyncFree(c_);
            throw RedisException(err);
    }
    c_->ev.addRead = redisAddRead;
    c_->ev.delRead = redisDelRead;
    c_->ev.addWrite = redisAddWrite;
    c_->ev.delWrite = redisDelWrite;
    c_->ev.cleanup = redisCleanup;
    c_->ev.data = this;
    c_->data = this;

    reading_ = false;
    writing_ = false;
    connected_ = false;

    rev_.set<AsyncContext, &AsyncContext::redisReadEvent>(this);
    rev_.set(c_->c.fd, ev::READ);
    wev_.set<AsyncContext, &AsyncContext::redisWriteEvent>(this);
    wev_.set(c_->c.fd, ev::WRITE);

    redisAsyncSetConnectCallback(c_, redisConnect);
    redisAsyncSetDisconnectCallback(c_, redisDisconnect);
}

AsyncContext::~AsyncContext() {
    FUTURES_DLOG(INFO) << "AsyncContext destroy: " << c_;
    rev_.stop();
    wev_.stop();
    if (c_) redisAsyncFree(c_);
}

void AsyncContext::redisReadEvent(ev::io &watcher, int revent) {
    if (revent & ev::ERROR)
        throw RedisException("failed to read");
    redisAsyncHandleRead(c_);
}

void AsyncContext::redisWriteEvent(ev::io &watcher, int revent) {
    if (revent & ev::ERROR)
        throw RedisException("failed to write");
    redisAsyncHandleWrite(c_);
}

void AsyncContext::redisAddRead(void *data) {
    AsyncContext *self = static_cast<AsyncContext*>(data);
    if (!self->reading_) {
        self->reading_ = true;
        self->rev_.start();
    }
}

void AsyncContext::redisDelRead(void *data) {
    AsyncContext *self = static_cast<AsyncContext*>(data);
    if (self->reading_) {
        self->reading_ = false;
        self->rev_.stop();
    }
}

void AsyncContext::redisAddWrite(void *data) {
    AsyncContext *self = static_cast<AsyncContext*>(data);
    if (!self->writing_) {
        self->writing_ = true;
        self->wev_.start();
    }
}

void AsyncContext::redisDelWrite(void *data) {
    AsyncContext *self = static_cast<AsyncContext*>(data);
    if (self->writing_) {
        self->writing_ = false;
        self->wev_.stop();
    }
}

void AsyncContext::redisCleanup(void *data) {
    AsyncContext *self = static_cast<AsyncContext*>(data);
    FUTURES_DLOG(INFO) << "redisCleanup";
    if (self) {
        redisDelRead(self);
        redisDelWrite(self);
    }
}

void AsyncContext::redisConnect(const redisAsyncContext *c, int status) {
    AsyncContext *self = static_cast<AsyncContext*>(c->data);
    FUTURES_DLOG(INFO) << "redis connect: " << status;
    if (status != REDIS_OK) {
        self->c_ = nullptr;
    } else {
        self->connected_ = true;
    }
}

void AsyncContext::redisDisconnect(const redisAsyncContext *c, int status) {
    AsyncContext *self = static_cast<AsyncContext*>(c->data);
    FUTURES_DLOG(INFO) << "redis disconnect: " << status;
    self->c_ = nullptr;
    self->connected_ = false;
    // pending should be empty
}

io::intrusive_ptr<AsyncContext::CompletionToken>
    AsyncContext::asyncFormattedCommand(const char *cmd, size_t len, bool subscribe) {
    reconnectIfNeeded();
    assert(c_);
    io::intrusive_ptr<CompletionToken> p(new CompletionToken(subscribe));
    int status = redisAsyncFormattedCommand(c_, redisCallback, p.get(), cmd, len);
    if (status) {
        p->reply_ = Try<Reply>(RedisException(c_->errstr));
        p->notifyDone();
    } else {
        p->attach(this);
        p->addRef();
    }
    return p;
}

void AsyncContext::redisCallback(struct redisAsyncContext* ctx, void *r, void *p) {
    AsyncContext *self = static_cast<AsyncContext*>(ctx->data);
    (void)self;
    auto handler = static_cast<CompletionToken*>(p);
    auto reply = static_cast<redisReply*>(r);
    if (!handler->subscribe_) {
        if (reply) {
            handler->reply_ = Try<Reply>(Reply(reply));
        } else {
            handler->reply_ = Try<Reply>(FutureCancelledException());
        }
        handler->notifyDone();
        handler->decRef();
    } else {
        if (reply) {
            // TODO unsubscribe
            handler->stream_.push_back(Try<Reply>(Reply(reply)));
            handler->notify();
        } else {
            handler->stream_.push_back(Try<Reply>(FutureCancelledException()));
            handler->notifyDone();
            handler->decRef();
        }
    }
}

void AsyncContext::onCancel(CancelReason r) {
    FUTURES_DLOG(INFO) << "canceling all requests";
    if (c_) {
        redisAsyncFree(c_);
        c_ = nullptr;
        connected_ = false;
    }
}

// RedisCommandFuture
RedisCommandFuture::RedisCommandFuture(AsyncContext::Ptr ctx, const char *format, ...)
    : ctx_(ctx) {
        va_list ap;
        int len;
        char *cmd;
        va_start(ap,format);
        len = redisvFormatCommand(&cmd, format, ap);
        va_end(ap);
        if (len < 0)
            throw RedisException("invalid command");
        assert(cmd != nullptr);
        cmd_.assign(cmd, len);
}

RedisCommandStream::RedisCommandStream(AsyncContext::Ptr ctx, const char *format, ...)
    : ctx_(ctx) {
        va_list ap;
        int len;
        char *cmd;
        va_start(ap,format);
        len = redisvFormatCommand(&cmd, format, ap);
        va_end(ap);
        if (len < 0)
            throw RedisException("invalid command");
        assert(cmd != nullptr);
        cmd_.assign(cmd, len);
}


}
}



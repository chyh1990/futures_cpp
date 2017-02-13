#include <futures/dns/Resolver.h>
#include <futures/dns/ResolverFuture.h>
#include <futures/Exception.h>

#include "udns/udns.h"
#include <unistd.h>
#include <fcntl.h>

namespace futures {
namespace dns {

inline std::error_code current_system_error(int e = errno) {
    return std::error_code(e, std::system_category());
}

AsyncResolver::AsyncResolver(EventExecutor *ev)
    : IOObject(ev), io_(ev->getLoop()), timer_(ev->getLoop()), check_(ev->getLoop()) {
    static struct dns_ctx * kDefaultCtx = [] () noexcept {
        dns_init(&dns_defctx, 0);
        return &dns_defctx;
    }();

    ctx_ = dns_new(kDefaultCtx);
    if (!ctx_)
        throw std::bad_alloc();
    dns_init(ctx_, 0);
    fd_ = dns_open(ctx_);
    if (fd_ < 0) {
        throw ResolverException("dns_open");
        dns_free(ctx_);
        ctx_ = nullptr;
    }
    FUTURES_DLOG(INFO) << "DNS sock: " << fd_;

    io_.set<AsyncResolver, &AsyncResolver::onEvent>(this);
    io_.start(fd_, ev::READ);

    timer_.set<AsyncResolver, &AsyncResolver::onTimer>(this);
    timer_.start(0.0);

    check_.set<AsyncResolver, &AsyncResolver::onPrepare>(this);
    check_.start();
}

void AsyncResolver::onEvent(ev::io &watcher, int revent) {
    if (revent & ev::ERROR)
        throw EventException("syscall error");
    if (revent & ev::READ)
        dns_ioevent(ctx_, 0);
}

void AsyncResolver::onTimer(ev::timer &watcher, int revent) {
    if (revent & ev::ERROR)
        throw EventException("syscall error");
    FUTURES_DLOG(INFO) << "onTimer";
    if (revent & ev::TIMER)
        dns_timeouts(ctx_, 30, 0);
}

void AsyncResolver::onPrepare(ev::prepare &watcher, int revent) {
    // flush requests before blocking
    if (revent & ev::PREPARE)
        dns_timeouts(ctx_, 30, 0);
}

io::intrusive_ptr<AsyncResolver::CompletionToken>
AsyncResolver::doResolve(const std::string &hostname, int flags) {
    if (!flags) throw std::invalid_argument("empty resolve flags");
    auto p = io::intrusive_ptr<CompletionToken>(new CompletionToken(this));
    if (flags & EnableTypeA4) {
        struct dns_query *q = dns_submit_a4(ctx_, hostname.c_str(), 0, queryA4Callback, p.get());
        p->q[TypeA4] = q;
    }
    if (flags & EnableTypeA6) {
        struct dns_query *q = dns_submit_a6(ctx_, hostname.c_str(), 0, queryA6Callback, p.get());
        p->q[TypeA6] = q;
    }
    p->checkPending();
    return p;
}

void AsyncResolver::doCancel(struct dns_query* q)
{
    FUTURES_DLOG(INFO) << "dns cancelled: " << q;
    if (q) {
        if (!dns_cancel(ctx_, q))
            free(q);
    }
}

void AsyncResolver::timerSetupCallback(struct dns_ctx *ctx, int timeout, void *data) {
    AsyncResolver *self = static_cast<AsyncResolver*>(data);
    FUTURES_DLOG(INFO) << "timerSetupCallback";
    self->timer_.stop();
    if (ctx && timeout >= 0) {
        self->timer_.set(timeout);
        self->timer_.start();
    }
}

void AsyncResolver::queryA4Callback(struct dns_ctx *ctx, struct dns_rr_a4 *result, void *data)
{
    int s = dns_status(ctx);
    CompletionToken *h = static_cast<CompletionToken*>(data);
    FUTURES_DLOG(INFO) << "queryA4Callback " << s;

    if (!s && result && result->dnsa4_addr) {
        for (int i = 0; i < result->dnsa4_nrr; ++i) {
            h->addrs_.push_back(folly::IPAddressV4(result->dnsa4_addr[i]));
        }
    }
    free(result);

    h->q[RecordType::TypeA4] = nullptr;
    h->checkPending();
}

void AsyncResolver::queryA6Callback(struct dns_ctx *ctx, struct dns_rr_a6 *result, void *data)
{
    int s = dns_status(ctx);
    CompletionToken *h = static_cast<CompletionToken*>(data);
    FUTURES_DLOG(INFO) << "queryA6Callback " << s;

    if (!s && result && result->dnsa6_addr) {
        for (int i = 0; i < result->dnsa6_nrr; ++i) {
            h->addrs_.push_back(folly::IPAddressV6(result->dnsa6_addr[i]));
        }
    }
    free(result);

    h->q[RecordType::TypeA6] = nullptr;
    h->checkPending();
}

AsyncResolver::~AsyncResolver() {
    io_.stop();
    timer_.stop();
    check_.stop();
    if (ctx_)
        dns_free(ctx_);
}

ResolverFuture AsyncResolver::resolve(const std::string &hostname, int flags) {
    return ResolverFuture(shared_from_this(), hostname, flags);
}

}
}

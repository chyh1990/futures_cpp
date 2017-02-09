#pragma once

#include <futures/core/SocketAddress.h>
#include <futures/io/IoFuture.h>
#include <futures/io/WaitHandleBase.h>
#include <futures/Promise.h>

struct dns_ctx;
struct dns_rr_a4;
struct dns_rr_a6;
struct dns_query;

namespace futures {
namespace dns {

class ResolverException : public std::runtime_error {
public:
    ResolverException(const std::string &s)
        : std::runtime_error(s) {}
};

using ResolverResult = std::vector<folly::IPAddress>;

class AsyncResolver : public io::IOObject {
public:
    enum RecordType {
        TypeA4 = 0,
        TypeA6 = 1,
    };

    enum ResolveFlags {
        EnableTypeA4 = 0x01,
        EnableTypeA6 = 0x02,
    };

    AsyncResolver(EventExecutor *ev);
    ~AsyncResolver();

    AsyncResolver(AsyncResolver&&) = delete;
    AsyncResolver& operator=(AsyncResolver&&) = delete;
    AsyncResolver(const AsyncResolver&) = delete;
    AsyncResolver& operator=(const AsyncResolver&) = delete;

    struct CompletionToken : public io::CompletionToken {
        std::array<struct dns_query*, 4> q;
        ResolverResult addrs_;

        CompletionToken(AsyncResolver *r)
            : io::CompletionToken(r) {
            q.fill(nullptr);
        }

        ~CompletionToken() {
            cleanup(0);
        }

        void checkPending() {
            if (!hasPending())
                notifyDone();
        }

        Poll<ResolverResult> poll() {
            auto v = pollState();
            if (v.hasException())
                return Poll<ResolverResult>(v.exception());
            if (*v) {
                return makePollReady(std::move(addrs_));
            } else {
                return Poll<ResolverResult>(not_ready);
            }
        }

        void onCancel() override {
            for (int i = 0; i < 4; ++i) {
                if (q[i]) {
                    static_cast<AsyncResolver*>(getIOObject())->doCancel(q[i]);
                    q[i] = nullptr;
                }
            }
        }
    private:
        bool hasPending() const {
            for (auto e: q)
                if (e) return true;
            return false;
        }
    };

    std::unique_ptr<CompletionToken> doResolve(const std::string &hostname, int flags);
    void doCancel(struct dns_query* q);

private:
    struct dns_ctx *ctx_;
    int fd_;

    ev::io io_;
    ev::timer timer_;
    ev::prepare check_;

    void onEvent(ev::io &watcher, int revent);
    void onTimer(ev::timer &watcher, int revent);
    void onPrepare(ev::prepare &watcher, int revent);

    static void queryA4Callback(struct dns_ctx *ctx, struct dns_rr_a4 *result, void *data);
    static void queryA6Callback(struct dns_ctx *ctx, struct dns_rr_a6 *result, void *data);
    static void timerSetupCallback(struct dns_ctx *ctx, int timeout, void *data);
};

class ResolverFuture : public FutureBase<ResolverFuture, ResolverResult> {
public:
    using Item = ResolverResult;

    ResolverFuture(std::shared_ptr<AsyncResolver> resolver,
            const std::string &hostname, int flags)
        : resolver_(resolver), hostname_(hostname), flags_(flags) {}

    Poll<Item> poll() override {
        if (!token_)
            token_ = resolver_->doResolve(hostname_, flags_);
        return token_->poll();
    }

private:
    std::shared_ptr<AsyncResolver> resolver_;
    std::string hostname_;
    int flags_;
    std::unique_ptr<AsyncResolver::CompletionToken> token_;
};

}
}

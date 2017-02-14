#pragma once

#include <futures/core/SocketAddress.h>
#include <futures/io/IoFuture.h>
#include <futures/io/WaitHandleBase.h>

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

class ResolverFuture;

class AsyncResolver :
    public io::IOObject,
    public std::enable_shared_from_this<AsyncResolver> {
public:
    using Ptr = std::shared_ptr<AsyncResolver>;

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
            : io::CompletionToken(r, IOObject::OpRead) {
            q.fill(nullptr);
        }

        void checkPending() {
            if (!hasPending())
                notifyDone();
        }

        Poll<ResolverResult> poll() {
            switch (getState()) {
            case STARTED:
                park();
                return Poll<ResolverResult>(not_ready);
            case DONE:
                return makePollReady(std::move(addrs_));
            case CANCELLED:
                return Poll<ResolverResult>(FutureCancelledException());
            }
        }

        void onCancel(CancelReason reason) override {
            for (int i = 0; i < 4; ++i) {
                if (q[i]) {
                    static_cast<AsyncResolver*>(getIOObject())->doCancel(q[i]);
                    q[i] = nullptr;
                }
            }
        }

    protected:
        ~CompletionToken() {
            cleanup(CancelReason::UserCancel);
        }

    private:
        bool hasPending() const {
            for (auto e: q)
                if (e) return true;
            return false;
        }
    };

    io::intrusive_ptr<CompletionToken> doResolve(const std::string &hostname, int flags);
    void doCancel(struct dns_query* q);

    ResolverFuture resolve(const std::string &hostname, int flags);

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

}
}

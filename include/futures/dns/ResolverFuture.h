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

class AsyncResolver {
public:
    enum RecordType {
        TypeA4 = 0,
        TypeA6 = 1,
    };

    enum ResolveFlags {
        EnableTypeA4 = 0x01,
        EnableTypeA6 = 0x02,
    };

    class WaitHandle : public io::WaitHandleBase<ResolverResult> {
    public:
        void cancel() {
            if (isReady()) return;
            for (int i = 0; i < 4; ++i)
                if (q[i]) resolver->doCancel(q[i]);
            // all callbacks removed from udns, drop reference
            release();
            set(Try<ResolverResult>(FutureCancelledException()));
        }

        WaitHandle(AsyncResolver *r) : resolver(r) {
            q.fill(nullptr);
        }

        bool hasPending() const {
            for (auto e: q) if (e) return true;
            return false;
        }

    private:
        AsyncResolver *resolver;
        std::array<struct dns_query*, 4> q;
        ResolverResult addrs_;

        friend class AsyncResolver;

        void doReady() {
            if (hasPending()) return;
            set(Try<ResolverResult>(std::move(addrs_)));
            unpark();
            release();
        }
    };

    AsyncResolver(EventExecutor *ev);
    ~AsyncResolver();

    io::wait_handle_ptr<WaitHandle> doResolve(const std::string &hostname, int flags);
    void doCancel(struct dns_query* q);

    AsyncResolver(AsyncResolver&&) = delete;
    AsyncResolver& operator=(AsyncResolver&&) = delete;
    AsyncResolver(const AsyncResolver&) = delete;
    AsyncResolver& operator=(const AsyncResolver&) = delete;
private:
    EventExecutor *ev_;
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

class ResolverFuture : public FutureBase<ResolverFuture,
    std::vector<folly::IPAddress>> {
    enum State {
        INIT,
        SUBMITTED,
        DONE,
    };
public:
    using Item = std::vector<folly::IPAddress>;

    ResolverFuture(std::shared_ptr<AsyncResolver> resolver, const std::string &hostname, int flags);

    ResolverFuture(ResolverFuture&&) = default;
    ResolverFuture& operator=(ResolverFuture&&) = default;
    ResolverFuture(const ResolverFuture&) = delete;
    ResolverFuture& operator=(const ResolverFuture&) = delete;

    Poll<Item> poll() override;

private:
    std::shared_ptr<AsyncResolver> resolver_;
    std::string hostname_;
    int flags_;
    State s_ = INIT;

    io::wait_handle_ptr<AsyncResolver::WaitHandle> handle_;
};

inline ResolverFuture resolve(std::shared_ptr<AsyncResolver> resolver,
        const std::string &name,
        int flags = AsyncResolver::EnableTypeA4)
{
    return ResolverFuture(resolver, name, flags);
}

}
}

#pragma once

#include <futures/dns/Resolver.h>

namespace futures {
namespace dns {

class ResolverFuture : public FutureBase<ResolverFuture, ResolverResult> {
public:
    using Item = ResolverResult;

    ResolverFuture(std::shared_ptr<AsyncResolver> resolver,
            const std::string &hostname, int flags)
        : resolver_(resolver), hostname_(hostname), flags_(flags) {}

    Poll<Item> poll() override {
        if (!token_.get())
            token_ = resolver_->doResolve(hostname_, flags_);
        return token_->poll();
    }

private:
    std::shared_ptr<AsyncResolver> resolver_;
    std::string hostname_;
    int flags_;
    io::intrusive_ptr<AsyncResolver::CompletionToken> token_;
};

}
}

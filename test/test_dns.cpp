#include <gtest/gtest.h>
#include <futures/io/IoStream.h>
#include <futures/EventExecutor.h>
#include <futures/Timer.h>
#include <futures/dns/ResolverFuture.h>

using namespace futures;

static BoxedFuture<folly::Unit> testDns(EventExecutor *ev, std::shared_ptr<dns::AsyncResolver> resolver) {
    return delay(ev, 0.5).andThen([ev, resolver] (std::error_code ec) {
            return timeout(ev, dns::ResolverFuture(resolver, "www.baidu.com",
                dns::AsyncResolver::EnableTypeA4|dns::AsyncResolver::EnableTypeA6), 1);
        })
        .then([] (Try<std::vector<folly::IPAddress>> v) {
            if (v.hasException()) {
                std::cerr << "err: " << v.exception().what() << std::endl;
            } else {
                for (auto &e: *v)
                    std::cerr << "addr: " << e.toJson() << std::endl;
            }
            // EventExecutor::current()->stop();
            return makeOk();
        }).boxed();
}

TEST(Resolver, TypeA) {
    EventExecutor ev;
    auto resolver = std::make_shared<dns::AsyncResolver>(&ev);
    // auto h = resolver.doResolve("www.baidu.com");
    // EXPECT_TRUE(h != nullptr);
    auto l = makeLoop(0, [&ev, resolver] (int i) {
        return testDns(&ev, resolver)
            .then([i] (Try<folly::Unit> err) {
                std::cerr << "Time: " << i << std::endl;
                if (i >= 5) {
                    EventExecutor::current()->stop();
                    return makeOk(makeBreak<folly::Unit, int>(folly::unit));
                }
                return makeOk(makeContinue<folly::Unit, int>(i+1));
            });
    });
    ev.spawn(std::move(l));

    ev.run(false);
}

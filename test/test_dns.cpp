#include <gtest/gtest.h>
#include <futures/EventExecutor.h>
#include <futures/Timeout.h>
#include <futures/dns/ResolverFuture.h>

using namespace futures;

static BoxedFuture<folly::Unit> testDns(EventExecutor *ev,
        TimerKeeper::Ptr timer,
        dns::AsyncResolver::Ptr resolver) {
    return delay(ev, 0.5).andThen([ev, timer, resolver] (folly::Unit) {
            return timeout(timer, resolver->resolve("www.baidu.com",
                dns::AsyncResolver::EnableTypeA4|dns::AsyncResolver::EnableTypeA6));
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
    auto timer = std::make_shared<TimerKeeper>(&ev, 1.0);
    auto l = makeLoop(0, [&ev, timer, resolver] (int i) {
        return testDns(&ev, timer, resolver)
            .then([i] (Try<Unit> err) {
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

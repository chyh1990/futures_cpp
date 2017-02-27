#include <gtest/gtest.h>
#include <futures/EventExecutor.h>
#include <futures/Stream.h>
#include <futures/CpuPoolExecutor.h>
#include <futures/channel/UnboundedMPSCChannel.h>
#include <futures/channel/BufferedChannel.h>
#include <futures/channel/ChannelStream.h>

using namespace futures;

TEST(Channel, MPSC1) {
    EventExecutor loop;
    CpuPoolExecutor cpu(1);

    int sum = 0;
    {
        auto pipe = channel::makeUnboundedMPSCChannel<int>();
        auto tx = std::move(pipe.first);
        auto rx = std::move(pipe.second);
        cpu.spawn_fn([tx] () mutable {
            for (int i = 0; i < 5; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                tx.send(i);
            }
            return makeOk();
        });

        auto printer = channel::makeReceiverStream(std::move(rx))
            .forEach([&sum] (int v) mutable {
                std::cerr << v << std::endl;
                sum += v;
            }).then([] (Try<folly::Unit>) {
                return makeOk();
            });

        loop.spawn(std::move(printer));
    }

    loop.run(false);
    EXPECT_EQ(sum, 10);
    FUTURES_DLOG(INFO) << "ENDED";
    cpu.stop();
}

TEST(Channel, Buffered) {
    auto ch = std::make_shared<channel::BufferedChannel<int>>(2);
    EventExecutor loop;
    CpuPoolExecutor cpu(1);

    cpu.spawn_fn([ch] () mutable {
        for (int i = 0; i < 5; ++i) {
            while (ch->trySend(i)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                break;
            }
        }
        return makeOk();
    });

    int sum = 0;
    auto printer = makeLoop(0, [ch, &sum] (int v) {
        if (v < 5) {
            return (ch->recv()
                >> [v, &sum] (int n) {
                    std::cerr << n << std::endl;
                    sum += n;
                    return makeOk(makeContinue<Unit, int>(v + 1));
                }).boxed();
        } else {
            return makeOk(makeBreak<Unit, int>(unit)).boxed();
        }
    });

    loop.spawn(std::move(printer));
    loop.run();
    EXPECT_EQ(sum, 10);

    cpu.stop();
}


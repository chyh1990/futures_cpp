#include <gtest/gtest.h>
#include <futures/Stream.h>
#include <futures/Timer.h>
#include <futures/TcpStream.h>

using namespace futures;

TEST(Stream, Empty) {
    EmptyStream<int> e;
    EXPECT_EQ(e.poll()->value(), Optional<int>());
}

TEST(Stream, Iter) {
    std::vector<std::string> v{"AAA", "BBB", "CCC"};
    auto s = makeIterStream(std::make_move_iterator(v.begin()),
            std::make_move_iterator(v.end()));
    auto f = s.forEach([] (std::string v) {
        std::cerr << v << std::endl;
    });

    f.wait();
}

static BoxedFuture<folly::Unit> process(EventExecutor &ev, tcp::Socket client) {
    auto c = folly::makeMoveWrapper(std::move(client));
    return delay(ev, 0.5)
        .andThen([&ev, c] (std::error_code ec) {
            return tcp::Stream::send(ev, c.move(),
                    folly::IOBuf::copyBuffer("TEST\n", 5, 0, 0));
        })
        .then([&ev] (Try<tcp::SendFutureItem> s) {
            ev.stop();
            return makeOk();
        }).boxed();
    // detail::resultOf<decltype(f), int>;
}

TEST(Stream, Listen) {
    std::error_code ec;
    tcp::Socket s;
    s.tcpServer("127.0.0.1", 8011, 32, ec);

    EXPECT_TRUE(!ec);

    EventExecutor loop;
    auto f = tcp::Stream::acceptStream(loop, s)
        .forEach([&loop] (tcp::Socket client) {
            std::cerr << "FD " << client.fd() << std::endl;
            loop.spawn(process(loop, std::move(client)));
        });
    loop.spawn(std::move(f));
    loop.run();
}

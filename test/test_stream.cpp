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

static BoxedFuture<folly::Unit> process(EventExecutor &ev, tcp::SocketPtr client) {
    return delay(&ev, 0.5)
        .andThen([&ev, client] (std::error_code ec) {
            return tcp::Stream::send(&ev, client,
                    folly::IOBuf::copyBuffer("TEST\n", 5, 0, 0));
        })
        .then([&ev] (Try<ssize_t> s) {
            ev.stop();
            return makeOk();
        }).boxed();
    // detail::resultOf<decltype(f), int>;
}

TEST(Stream, Listen) {
    std::error_code ec;
    auto s = std::make_shared<tcp::Socket>();
    s->tcpServer("127.0.0.1", 8011, 32, ec);

    EXPECT_TRUE(!ec);

    EventExecutor loop;
    auto f = tcp::Stream::acceptStream(&loop, s)
        .forEach([&loop] (tcp::SocketPtr client) {
            std::cerr << "FD " << client->fd() << std::endl;
            loop.spawn(process(loop, client));
        });
    loop.spawn(std::move(f));
    loop.run();
}

class IntCodec: public io::Codec<IntCodec, int64_t, int64_t> {
public:
    using In = int64_t;
    using Out = int64_t;

    IntCodec() {
        buf_.resize(128);
    }

    Try<Optional<In>> decode(std::unique_ptr<folly::IOBuf> &buf) {
        auto p = std::find(buf->data(), buf->tail(), '\n');
        if (p == buf->tail())
            return Try<Optional<In>>(folly::none);
        size_t len = p - buf->data();
        buf_.assign((const char*)buf->data(), len);
        FUTURES_DLOG(INFO) << "GET: " << len << ", "
            << buf_ << ", " << buf_.capacity();
        buf->trimStart(len + 1);
        return folly::makeTryWith([&] () {
                int64_t v = std::stol(buf_);
                return Optional<In>(v);
        });
    }

private:
    std::string buf_;
};

static BoxedFuture<folly::Unit> process_frame(EventExecutor &ev, tcp::SocketPtr client) {
    return io::Framed<IntCodec>(folly::make_unique<tcp::SocketIOHandler>(&ev, client))
            .forEach([] (int64_t v) {
                std::cerr << "V: " << v << std::endl;
            })
            .then([] (Try<folly::Unit> err) {
                if (err.hasException()) {
                    std::cerr << "ERR: " << err.exception().what() << std::endl;
                }
                return makeOk();
            })
            .boxed();
}

TEST(Stream, Frame) {
    std::error_code ec;
    auto s = std::make_shared<tcp::Socket>();
    s->tcpServer("127.0.0.1", 8011, 32, ec);

    EXPECT_TRUE(!ec);

    EventExecutor loop;
    auto f = tcp::Stream::acceptStream(&loop, s)
        .forEach([&loop] (tcp::SocketPtr client) {
            std::cerr << "FD " << client->fd() << std::endl;
            loop.spawn(process_frame(loop, client));
        });
    loop.spawn(std::move(f));
    loop.run();
}

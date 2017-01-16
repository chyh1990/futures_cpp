#include <gtest/gtest.h>
#include <futures/Stream.h>
#include <futures/Timer.h>
#include <futures/TcpStream.h>
#include <futures/http/HttpCodec.h>
#include <futures/io/PipelinedRpcFuture.h>

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

    Try<folly::Unit> encode(const Out& out,
            std::unique_ptr<folly::IOBuf> &buf) {
        const int kMaxLen = 32;
        buf->reserve(0, kMaxLen);
        int r = ::snprintf((char*)buf->tailroom(), 32, "%ld\n", out);
        if ((r == -1) || (r >= kMaxLen))
            return Try<folly::Unit>(IOError("number too long"));
        return Try<folly::Unit>();
    }
private:
    std::string buf_;
};

static BoxedFuture<folly::Unit> process_frame(EventExecutor &ev, tcp::SocketPtr client) {
    return io::FramedStream<IntCodec>(
            folly::make_unique<tcp::SocketIOHandler>(&ev, client))
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

class DummyService: public Service<http::Request, http::Response> {
public:
    BoxedFuture<http::Response> operator()(http::Request req) {
        std::cerr << req << std::endl;
        http::Response resp;
        // resp.http_errno = 200;
        // resp.body = "XXXXX";
        return makeOk(std::move(resp)).boxed();
    }
};

static BoxedFuture<folly::Unit> process_http(EventExecutor &ev, tcp::SocketPtr client) {
#if 0
    return io::FramedStream<http::HttpV1Codec>(folly::make_unique<tcp::SocketIOHandler>(&ev, client))
            .forEach([] (http::Result v) {
                std::cerr << "V: " << v << std::endl;
            })
            .then([] (Try<folly::Unit> err) {
                if (err.hasException()) {
                    std::cerr << "ERR: " << err.exception().what() << std::endl;
                }
                return makeOk();
            })
            .boxed();
#endif
    io::FramedStream<http::HttpV1Codec> source(folly::make_unique<tcp::SocketIOHandler>(&ev, client));
    io::FramedSink<http::HttpV1Codec> sink(folly::make_unique<tcp::SocketIOHandler>(&ev, client));
    auto service = std::make_shared<DummyService>();
    return PipelinedRpcFuture<http::HttpV1Codec>(service, std::move(source), std::move(sink)).boxed();
}

TEST(Stream, Http) {
    std::error_code ec;
    auto s = std::make_shared<tcp::Socket>();
    s->tcpServer("127.0.0.1", 8011, 32, ec);

    EXPECT_TRUE(!ec);

    EventExecutor loop;
    auto f = tcp::Stream::acceptStream(&loop, s)
        .forEach([&loop] (tcp::SocketPtr client) {
            std::cerr << "FD " << client->fd() << std::endl;
            loop.spawn(process_http(loop, client));
        });
    loop.spawn(std::move(f));
    loop.run();

}


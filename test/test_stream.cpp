#include <gtest/gtest.h>
#include <futures/Stream.h>
#include <futures/Timer.h>
#include <futures/TcpStream.h>
#include <futures/http/HttpCodec.h>
#include <futures/io/PipelinedRpcFuture.h>
#include <futures/io/IoStream.h>
#include <futures/channel/UnboundedMPSCChannel.h>
#include <futures/channel/ChannelStream.h>
#include <futures/CpuPoolExecutor.h>
#include <futures/io/AsyncSocket.h>
#include <futures/io/AsyncServerSocket.h>

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

#if 0
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

class IntDecoder: public io::DecoderBase<IntDecoder, int64_t> {
public:
    using Out = int64_t;

    IntDecoder() {
        buf_.resize(128);
    }

    Try<Optional<Out>> decode(folly::IOBufQueue &buf) {
#if 0
        assert(!buf.empty());
        auto p = std::find(buf.front()->data(), buf.front()->tail(), '\n');
        if (p == buf.front()->tail())
            return Try<Optional<Out>>(folly::none);
        size_t len = p - buf->data();
        buf_.assign((const char*)buf->data(), len);
        FUTURES_DLOG(INFO) << "GET: " << len << ", "
            << buf_ << ", " << buf_.capacity();
        buf->trimStart(len + 1);
        return folly::makeTryWith([&] () {
                int64_t v = std::stol(buf_);
                return Optional<Out>(v);
        });
#endif
    }

#if 0
    Try<void> encode(const Out& out,
            std::unique_ptr<folly::IOBuf> &buf) {
        const int kMaxLen = 32;
        buf->reserve(0, kMaxLen);
        int r = ::snprintf((char*)buf->tailroom(), 32, "%ld\n", out);
        if ((r == -1) || (r >= kMaxLen))
            return Try<folly::Unit>(IOError("number too long"));
        return Try<folly::Unit>();
    }
#endif
private:
    std::string buf_;
};

static BoxedFuture<folly::Unit> process_frame(EventExecutor &ev, tcp::SocketPtr client) {
    return io::FramedStream<IntDecoder>(
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
#if 1
    return io::FramedStream<http::HttpV1Decoder>(folly::make_unique<tcp::SocketIOHandler>(&ev, client))
            .forEach([] (http::Request v) {
                std::cerr << "V: " << v << std::endl;
            })
            .then([] (Try<folly::Unit> err) {
                if (err.hasException()) {
                    std::cerr << "ERR: " << err.exception().what() << std::endl;
                }
                return makeOk();
            })
            .boxed();
#else
    io::FramedStream<http::HttpV1Codec> source(folly::make_unique<tcp::SocketIOHandler>(&ev, client));
    io::FramedSink<http::HttpV1Codec> sink(folly::make_unique<tcp::SocketIOHandler>(&ev, client));
    auto service = std::make_shared<DummyService>();
    return PipelinedRpcFuture<http::HttpV1Codec>(service, std::move(source), std::move(sink)).boxed();
#endif
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
#endif

TEST(Stream, Channel) {
    EventExecutor loop;
    CpuPoolExecutor cpu(1);

    {
        auto pipe = channel::makeUnboundedMPSCChannel<int>();
        auto tx = std::move(pipe.first);
        auto rx = std::move(pipe.second);
        cpu.spawn_fn([tx] () mutable {
            for (int i = 0; i < 3; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                tx.send(i);
            }
            return makeOk();
        });

        auto printer = channel::makeReceiverStream(std::move(rx))
            .forEach([] (int v) {
                std::cerr << v << std::endl;
            }).then([] (Try<folly::Unit>) {
                EventExecutor::current()->stop();
                return makeOk();
            });

        loop.spawn(std::move(printer));
    }

    loop.run(true);
    FUTURES_DLOG(INFO) << "ENDED";
    cpu.stop();
}

TEST(IO, NewSocket) {
    EventExecutor ev;

    auto f = io::SocketChannel::connect(&ev, folly::SocketAddress("127.0.0.1", 8011))
        .andThen([] (io::SocketChannel::Ptr sock) {
            return sock->readStream()
            .forEach([sock] (std::unique_ptr<folly::IOBuf> buf) {
                    std::cerr << "READ: " << buf->computeChainDataLength() << std::endl;
                    EventExecutor::current()->spawn(
                            sock->write(std::move(buf))
                                .error([] (folly::exception_wrapper w) {
                                    std::cerr << "ERR: " << w.what() << std::endl;
                                })
                    );
            });
        })
        .then([] (Try<folly::Unit> err) {
            if (err.hasException())
                std::cerr << err.exception().what() << std::endl;
            return makeOk();
        });
    ev.spawn(std::move(f));
    ev.run();
}

static BoxedFuture<folly::Unit> doEcho(io::SocketChannel::Ptr sock) {
    return io::SockWriteFuture(sock, folly::IOBuf::copyBuffer("XXX", 3))
        .error([] (folly::exception_wrapper w) {
            std::cerr << "WRITE_ERR: " << w.what() << std::endl;
        }).boxed();
}

TEST(IO, Accept) {
    EventExecutor ev;
    folly::SocketAddress addr("127.0.0.1", 8033);
    auto p = std::make_shared<io::AsyncServerSocket>(&ev, addr);

    int cnt = 0;
    auto f = p->accept()
        .forEach2([&cnt] (tcp::Socket sock, folly::SocketAddress peer) mutable {
            std::cerr << "accept from: " << peer.getAddressStr();
            auto ev = EventExecutor::current();
            auto new_sock = std::make_shared<io::SocketChannel>(ev, std::move(sock), peer);
            ev->spawn(doEcho(new_sock));
            cnt ++;
            if (cnt > 1)
                ev->stop();
    }).error([] (folly::exception_wrapper err) {
        std::cerr << err.what() << std::endl;
    });
    ev.spawn(std::move(f));
    ev.run();
}


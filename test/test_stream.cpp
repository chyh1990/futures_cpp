#include <gtest/gtest.h>
#include <futures/Stream.h>
#include <futures/Timer.h>
#include <futures/TcpStream.h>
#include <futures/http/HttpCodec.h>
#include <futures/service/RpcFuture.h>
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
    auto v_old = v;
    auto s = makeIterStream(std::make_move_iterator(v.begin()),
            std::make_move_iterator(v.end()));
    auto f = s.collect();
    std::vector<std::string> v1 = f.value();
    EXPECT_EQ(v_old, v1);
}

TEST(Stream, Filter) {
    std::vector<std::string> v{"AAA", "BBB1", "CCC"};
    auto s = makeIterStream(std::make_move_iterator(v.begin()),
            std::make_move_iterator(v.end()))
        .filter([] (const std::string &s) {
            return s.size() == 3;
        });

    auto f = s.collect();
    std::vector<std::string> v1 = f.value();
    const std::vector<std::string> kResult{"AAA", "CCC"};
    EXPECT_EQ(v1, kResult);
}

TEST(Stream, Map) {
    std::vector<std::string> v{"AAA", "BBB1", "CCC"};
    auto s = makeIterStream(std::make_move_iterator(v.begin()),
            std::make_move_iterator(v.end()))
        .map([] (const std::string &s) {
            return s.length();
        });

    auto f = s.collect();
    std::vector<size_t> v1 = f.value();
    const std::vector<size_t> kResult{3, 4, 3};
    EXPECT_EQ(v1, kResult);
}

TEST(Stream, AndThen) {
    std::vector<std::string> v{"AAA", "BBB1", "CCC"};
    auto s = makeIterStream(std::make_move_iterator(v.begin()),
            std::make_move_iterator(v.end()))
        .andThen([] (const std::string &s) {
            return makeOk(s.length());
        });

    auto f = s.collect();
    std::vector<size_t> v1 = f.value();
    const std::vector<size_t> kResult{3, 4, 3};
    EXPECT_EQ(v1, kResult);
}

TEST(Stream, Take) {
    const std::vector<int> v{0, 1, 2};
    auto s = makeIterStream(v.begin(), v.end()).take(2);
    const std::vector<int> kResult{0,1};
    EXPECT_EQ(s.collect().value(), kResult);

    auto s1 = makeIterStream(v.begin(), v.end()).take(10);
    EXPECT_EQ(s1.collect().value(), v);
}

TEST(Stream, Iterator) {
    std::vector<int> v{0, 1, 2};
    auto s = makeIterStream(v.begin(), v.end());
    int i = 0;
    for (auto &e: s) {
        EXPECT_EQ(e, i);
        i++;
    }
}

TEST(StreamIO, NewSocket) {
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

TEST(StreamIO, Accept) {
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


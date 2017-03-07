#include <gtest/gtest.h>
#include <futures/io/AsyncSSLSocket.h>
#include <futures/io/StreamAdapter.h>

using namespace futures;

TEST(IO, SSL) {
    EventExecutor ev;
    io::SSLContext ctx;

    folly::SocketAddress addr("192.30.253.113", 443);
#if 0
    auto f = io::SSLSocketChannel::connect(&ev, &ctx, addr)
        >> [] (io::SSLSocketChannel::Ptr sock) {
            FUTURES_DLOG(INFO) << "connected";
            sock->printPeerCert();
            const char req[] = "GET / HTTP/1.1\r\nHost: github.com\r\nUser-Agent: curl/7.35.0\r\n\r\n";
            return io::SockWriteFuture(sock, folly::IOBuf::copyBuffer(req, sizeof(req)));

        >> [sock] (ssize_t size) {
            FUTURES_DLOG(INFO) << "written: " << size;
            return io::SockReadStream(sock)
                .forEach([] (std::unique_ptr<folly::IOBuf> q) {
                        // FUTURES_DLOG(INFO) << "chain size: " << q->computeChainDataLength();
                        folly::IOBufQueue t;
                        t.append(std::move(q));
                        IOBufStreambuf buf(&t);
                        std::istream is(&buf);
                        std::cerr << "=============" << std::endl;
                        std::cerr << is.rdbuf() << std::endl;
                        std::cerr << "=============" << std::endl;
                });
        }
        }
        << [] (Try<Unit> err) {
            if (err.hasException())
                FUTURES_LOG(ERROR) << err.exception().what();
            else
                FUTURES_LOG(INFO) << "SSL done";
            EventExecutor::current()->stop();
            return makeOk();
        };
#endif
    auto f = io::SSLSocketChannel::connect(&ev, &ctx, addr)
        >> [] (io::SSLSocketChannel::Ptr sock) {
            const char req[] = "GET / HTTP/1.1\r\nHost: github.com\r\nUser-Agent: curl/7.35.0\r\n\r\n";
            return io::WriteFuture(sock, folly::IOBuf::copyBuffer(req, sizeof(req)))
                >> [sock] (size_t size) {
                    FUTURES_DLOG(INFO) << "written: " << size;
                    return io::ReadStream(sock)
                    .forEach([] (std::unique_ptr<folly::IOBuf> q) {
                            // FUTURES_DLOG(INFO) << "chain size: " << q->computeChainDataLength();
                            folly::IOBufQueue t;
                            t.append(std::move(q));
                            IOBufStreambuf buf(&t);
                            std::istream is(&buf);
                            std::cerr << "=============" << std::endl;
                            std::cerr << is.rdbuf() << std::endl;
                            std::cerr << "=============" << std::endl;
                    });
                };
        }
        << [] (Try<Unit> err) {
            if (err.hasException())
                FUTURES_LOG(ERROR) << err.exception().what();
            else
                FUTURES_LOG(INFO) << "SSL done";
            EventExecutor::current()->stop();
            return makeOk();
        };


    ev.spawn(std::move(f));
    ev.run(true);
}

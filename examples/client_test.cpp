#include <futures/EventExecutor.h>
#include <futures/Signal.h>
#include <futures/Timer.h>
#include <futures/io/AsyncSocket.h>
#include <futures/io/PipelinedRpcFuture.h>
#include <futures/http/HttpCodec.h>

using namespace futures;

template <typename ReadStream, typename WriteSink, typename Dispatch>
RpcFuture<ReadStream, WriteSink>
makeRpcClientFuture(io::Channel::Ptr transport,
        std::shared_ptr<Dispatch> dispatch) {
    using Req = typename ReadStream::Item;
    using Resp = typename WriteSink::Out;
    return RpcFuture<ReadStream, WriteSink>(
            transport,
            dispatch);
}

int main(int argc, char *argv[])
{
    EventExecutor loop(true);
    folly::SocketAddress addr("127.0.0.1", 8022);
    auto sock = std::make_shared<io::SocketChannel>(&loop);
    auto f = io::ConnectFuture(sock, addr)
        .andThen([sock] (folly::Unit) {
            FUTURES_LOG(INFO) << "connected";
            auto client = std::make_shared<PipelineClientDispatcher<http::Response, http::Request>>();
            EventExecutor::current()->spawn(
                    makeRpcClientFuture<io::FramedStream<http::HttpV1Decoder>,
                    io::FramedSink<http::HttpV1Encoder>>(sock, client));
            http::Response r;
            r.headers["XXX"] = "BBB";
            return (*client)(std::move(r))
                .then([client] (Try<http::Request> req) {
                    if (req.hasException()) {
                        std::cerr << "CALL: " << req.exception().what() << std::endl;
                    } else {
                        std::cerr << *req << std::endl;
                    }
                    return client->close();
                });
        }).error([] (folly::exception_wrapper w) {
            std::cerr << "OUT: " << w.what() << std::endl;
        });
    loop.spawn(std::move(f));
    loop.run();
    return 0;
}


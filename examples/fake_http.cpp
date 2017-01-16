#include <futures/EventExecutor.h>
#include <futures/Signal.h>
#include <futures/TcpStream.h>
#include <futures/http/HttpCodec.h>
#include <futures/io/PipelinedRpcFuture.h>
#include <thread>
#include <iostream>

using namespace futures;

class DummyService: public Service<http::Request, http::Response> {
public:
    BoxedFuture<http::Response> operator()(http::Request req) {
        // std::cerr << req << std::endl;
        http::Response resp;
        // resp.http_errno = 200;
        resp.body = "XXXXX";
        return makeOk(std::move(resp)).boxed();
    }
};

static BoxedFuture<folly::Unit> process(EventExecutor *ev, tcp::SocketPtr client) {
    io::FramedStream<http::HttpV1Codec> source(folly::make_unique<tcp::SocketIOHandler>(ev, client));
    io::FramedSink<http::HttpV1Codec> sink(folly::make_unique<tcp::SocketIOHandler>(ev, client));
    auto service = std::make_shared<DummyService>();
    return PipelinedRpcFuture<http::HttpV1Codec>(service, std::move(source), std::move(sink))
        .then([] (Try<folly::Unit> err) {
            if (err.hasException())
                std::cerr << err.exception().what() << std::endl;
            return makeOk();
        })
        .boxed();
}


int main(int argc, char *argv[])
{
    std::error_code ec;
    auto s = std::make_shared<tcp::Socket>();
    s->tcpServer("127.0.0.1", 8011, 32, ec);
    assert(!ec);
    const int kWorkers = 4;

    EventExecutor loop(true);
    std::unique_ptr<EventExecutor> worker_loops[kWorkers];
    for (int i = 0; i < kWorkers; i++)
        worker_loops[i].reset(new EventExecutor());

    std::cerr << "listening: " << 8011 << std::endl;
    auto f = tcp::Stream::acceptStream(&loop, s)
        .forEach([&worker_loops] (tcp::SocketPtr client) {
                auto loop = worker_loops[rand() % kWorkers].get();
                // auto loop = EventExecutor::current();
                loop->spawn(process(loop, client));
                });
    auto sig = signal(&loop, SIGINT)
        .andThen([&] (int signum) {
                std::cerr << "killed by " << signum << std::endl;
                EventExecutor::current()->stop();
                for (int i = 0; i < kWorkers; ++i) {
                worker_loops[i]->spawn(makeLazy([] () {
                            EventExecutor::current()->stop();
                            return folly::unit;
                        }));
                }
                return makeOk();
                });
    loop.spawn(std::move(f));
    loop.spawn(std::move(sig));

    std::vector<std::thread> workers;
    for (int i = 0 ;i <  kWorkers; ++i) {
        auto worker = std::thread([&worker_loops, i] () {
                worker_loops[i]->run(true);
                });
        workers.push_back(std::move(worker));
    }
    loop.run();
    for (auto &e: workers)
        e.join();

    return 0;
}

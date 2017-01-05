#include <futures/EventExecutor.h>
#include <futures/Signal.h>
#include <futures/TcpStream.h>
#include <thread>
#include <iostream>

using namespace futures;

const std::string kResponse = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHELLO";

static BoxedFuture<folly::Unit> process(EventExecutor *ev, tcp::Socket client) {
    return tcp::Stream::recv(ev,
            std::move(client), tcp::TransferAtLeast(10, 1024))
        .andThen2([] (tcp::Socket s, std::unique_ptr<folly::IOBuf> buf) {
            auto buf1 = folly::IOBuf::copyBuffer(kResponse.data(), kResponse.size(), 0, 64);
            return tcp::Stream::send(EventExecutor::current(),
                    std::move(s), std::move(buf1));
        })
        .then([] (Try<tcp::SendFutureItem> s) {
            if (s.hasException())
                std::cerr << "ERROR: " << s.exception().what() << std::endl;
            return makeOk();
        }).boxed();
}


int main(int argc, char *argv[])
{
    std::error_code ec;
    tcp::Socket s;
    s.tcpServer("127.0.0.1", 8011, 32, ec);
    assert(!ec);
    const int kWorkers = 2;

    EventExecutor loop(true);
    std::unique_ptr<EventExecutor> worker_loops[kWorkers];
    for (int i = 0; i < kWorkers; i++)
        worker_loops[i].reset(new EventExecutor());

    std::cerr << "listening: " << 8011 << std::endl;
    auto f = tcp::Stream::acceptStream(&loop, s)
        .forEach([&worker_loops] (tcp::Socket client) {
            auto loop = worker_loops[rand() % kWorkers].get();
            // auto loop = EventExecutor::current();
            loop->spawn(process(loop, std::move(client)));
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

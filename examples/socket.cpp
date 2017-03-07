#include <futures/EventExecutor.h>
#include <futures/Timer.h>
#include <futures/io/Signal.h>
#include <futures/TcpStream.h>
#include <iostream>

using namespace futures;


struct SharedState {
    size_t counter = 0;
};

#if 0
static BoxedFuture<folly::Unit> process(EventExecutor &ev,
        std::shared_ptr<SharedState> state, tcp::SocketPtr client) {
    return delay(&ev, 0.5)
        .andThen([&ev, client, state] (std::error_code ec) {
            state->counter ++;
            auto buf = folly::IOBuf::copyBuffer("TEST ", 5, 0, 64);
            auto nlen = ::snprintf((char*)buf->writableTail(),
                    64, "%ld\n", state->counter);
            if (nlen < 0)
                throw std::runtime_error("buffer overflow");
            buf->append(nlen);
            return tcp::Stream::send(&ev, client, std::move(buf));
        })
        .then([&ev] (Try<ssize_t> s) {
            if (s.hasException())
                std::cerr << "ERROR: " << s.exception().what() << std::endl;
            return makeOk();
        }).boxed();
}


int main(int argc, char *argv[])
{
    std::error_code ec;
    auto s = std::make_shared<tcp::Socket>();
    s->tcpServer("127.0.0.1", 8011, 32, ec);
    assert(!ec);

    EventExecutor loop;
    auto state = std::make_shared<SharedState>();

    auto f = tcp::Stream::acceptStream(&loop, s)
        .forEach([&loop, state] (tcp::SocketPtr client) {
            std::cerr << "new client: " << client->fd() << std::endl;
            loop.spawn(process(loop, state, client));
        });
    auto sig = signal(&loop, SIGINT)
        .andThen([&loop] (int signum) {
            std::cerr << "killed by " << signum << std::endl;
            loop.stop();
            return makeOk();
        });
    loop.spawn(std::move(f));
    loop.spawn(std::move(sig));
    loop.run();

    return 0;
}
#endif

int main(int argc, char *argv[])
{
    return 0;
}

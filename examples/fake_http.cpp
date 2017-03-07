#include <futures/EventExecutor.h>
#include <futures/io/Signal.h>
#include <futures/Timer.h>
#include <futures/TcpStream.h>
#include <futures/http/HttpCodec.h>
#include <futures/http/HttpController.h>
#include <futures/service/RpcFuture.h>
#include <futures/io/AsyncSocket.h>
#include <futures/io/AsyncServerSocket.h>
#include <futures/EventThreadPool.h>
#include <thread>
#include <iostream>
#include <futures/io/StreamAdapter.h>
#include "json.hpp"

using namespace futures;


class SampleService : public http::HttpController {
public:
  SampleService() {
    setup();
  }

private:
  void setup() {
    get("^/test$", [] (http::HttpRequest req) {
        http::Response resp;
        resp.http_errno = 200;
        resp.body.append("Hello", 5);
        return makeOk(std::move(resp)); // .boxed();
    });

    post("^/sleep$", [] (http::HttpRequest req) {
        return
          delay(EventExecutor::current(), 1.0)
          >> [] (Unit) {
              http::Response resp;
              resp.http_errno = 200;
              resp.body.append("Done", 4);
              return makeOk(std::move(resp));
            };
    });

    post("^/json$", [] (http::HttpRequest req) {
        if (!req.raw.hasContentLength())
          throw std::invalid_argument("no content");
        nlohmann::json j;
        IOBufStreambuf input_buf(&req.raw.body);
        std::istream is(&input_buf);
        j << is;

        http::Response resp;
        resp.http_errno = 200;

        IOBufStreambuf outbuf(&resp.body);
        std::ostream os(&outbuf);
        os << j.dump(2) << std::endl;

        return makeOk(std::move(resp));
    });
  }
};

static BoxedFuture<folly::Unit> process(EventExecutor *ev,
    io::SocketChannel::Ptr client,
    std::shared_ptr<SampleService> service) {
    using HttpStream = io::FramedStream<http::Request>;
    using HttpSink = io::FramedSink<http::Response>;
    return service::makePipelineRpcFuture(
      client,
      HttpStream(client, std::make_shared<http::HttpV1RequestDecoder>()),
      HttpSink(client, std::make_shared<http::HttpV1ResponseEncoder>()),
      service)
    << [] (Try<folly::Unit> err) {
      if (err.hasException())
        std::cerr << err.exception().what() << std::endl;
      return makeOk();
    };
}

int main(int argc, char *argv[])
{

  EventExecutor loop(true);

  folly::SocketAddress bindAddr("127.0.0.1", 8011);
  auto s = std::make_shared<io::AsyncServerSocket>(&loop, bindAddr);
  const int kWorkers = 4;
  EventThreadPool pool(kWorkers);

  auto pservice = std::make_shared<SampleService>();

  std::cerr << "listening: " << 8011 << std::endl;
  auto f = s->accept()
    .forEach2([&pool, pservice] (tcp::Socket client, folly::SocketAddress peer) {
        // std::cerr << "accept from: " << peer.getAddressStr() << ":" << peer.getPort();
        auto loop = pool.getExecutor();
        auto new_sock = std::make_shared<io::SocketChannel>(loop, std::move(client), peer);
        // auto loop = EventExecutor::current();
        loop->spawn(process(loop, new_sock, pservice));
        // loop->spawn(processWs(loop, client, pWsservice));
        })
  .then([] (Try<folly::Unit> err) {
      if (err.hasException())
        std::cerr << "Error: " << err.exception().what() << std::endl;
      return makeOk();
      });
  auto sig = io::signal(&loop, SIGINT)
    >> [&pool] (int signum) {
        std::cerr << "killed by " << signum << std::endl;
        EventExecutor::current()->stop();
        pool.stop();
        return makeOk();
      };
  loop.spawn(std::move(f));
  loop.spawn(std::move(sig));

  pool.start();
  loop.run();
  pool.join();

  return 0;
}

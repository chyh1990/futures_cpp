#include <futures/EventExecutor.h>
#include <futures/io/Signal.h>
#include <futures/TimerKeeper.h>
#include <futures/codec/LineBasedDecoder.h>
#include <futures/codec/StringEncoder.h>
#include <futures/io/AsyncServerSocket.h>
#include <futures/io/AsyncSocket.h>
#include <futures/service/Service.h>
#include <futures/service/RpcFuture.h>

using namespace futures;

using Req = std::unique_ptr<folly::IOBuf>;

class EchoService : public service::Service<Req, std::string> {
public:
  EchoService(EventExecutor *ev)
    : timer_(std::make_shared<TimerKeeper>(ev, 0.5)) {}

  BoxedFuture<std::string> operator()(Req req) override {
    auto m = folly::makeMoveWrapper(req);
    return timer_->timeout()
      >> [m] (Unit) {
        auto s = *m ? m.move()->coalesce().toString() : "";
        s.push_back('\r');
        s.push_back('\n');
        return makeOk(s);
      };
  }

  BoxedFuture<Unit> close() override {
    FUTURES_DLOG(INFO) << "closing";
    return makeOk();
  }

private:
  TimerKeeper::Ptr timer_;
};

class Timer : public std::enable_shared_from_this<Timer> {
public:
  using Ptr = std::shared_ptr<Timer>;

  Timer(TimerKeeper::Ptr timer)
    : timer_(timer) {
    tok_ = timer->doTimeout();
  }

  void stop() {
    if (tok_->getState() == TimerKeeper::CompletionToken::STARTED) {
      FUTURES_DLOG(INFO) << "stopped";
      tok_->stop();
    }
  }

  ~Timer() {
    stop();
  }

#if 0
  void restart(double after) {
  }
#endif

  TimerKeeperFuture wait() {
    return TimerKeeperFuture(timer_, tok_);
  }

private:
  TimerKeeper::Ptr timer_;
  io::intrusive_ptr<TimerKeeper::CompletionToken> tok_;
};

class ExpiringFilter : public service::ServiceFilter<Req, std::string> {
public:
  ExpiringFilter(TimerKeeper::Ptr timer, std::shared_ptr<EchoService> s)
    : service::ServiceFilter<Req, std::string>(s),
      keeper_(timer)
  {
    startIdleTimer();
  }

  BoxedFuture<std::string> operator()(Req req) override
  {
    if (timer_) timer_.reset();
    req_++;
    return (*service_)(std::move(req))
      | [this] (std::string s) {
        req_--;
        startIdleTimer();
        return s;
      };
  }

private:
  uint64_t req_{0};
  TimerKeeper::Ptr keeper_;
  Timer::Ptr timer_;
  // Optional<BoxedFuture<Unit>> idle_;

  void startIdleTimer() {
    if (req_ > 0) return;
    timer_ = std::make_shared<Timer>(keeper_);
    auto f = timer_->wait()
      >> [this] (Unit) {
        FUTURES_LOG(INFO) << "idle timeout";
        return this->close();
      };
    keeper_->getExecutor()->spawn(f);
  }
};

static BoxedFuture<folly::Unit> process(EventExecutor *ev,
    io::SocketChannel::Ptr client,
    std::shared_ptr<EchoService> service) {
    using Stream = io::FramedStream<Req>;
    using Sink = io::FramedSink<std::string>;
    auto timer = std::make_shared<TimerKeeper>(ev, 3.0);
    auto ex_service = std::make_shared<ExpiringFilter>(timer, service);
    return service::makePipelineRpcFuture(
      client,
      Stream(client, std::make_shared<codec::LineBasedDecoder>()),
      Sink(client, std::make_shared<codec::StringEncoder>()),
      ex_service)
    .error([] (folly::exception_wrapper err) {
        FUTURES_LOG(ERROR) << err.what();
        return makeOk();
    });
}

int main(int argc, char *argv[])
{
  EventExecutor loop(true);

  folly::SocketAddress bindAddr("127.0.0.1", 8011);
  auto s = std::make_shared<io::AsyncServerSocket>(&loop, bindAddr);
  auto service = std::make_shared<EchoService>(&loop);

  auto f = s->accept()
    .forEach2([service] (tcp::Socket client, folly::SocketAddress peer) {
        FUTURES_DLOG(INFO) << "client: " << peer.getAddressStr();
        auto loop = EventExecutor::current();
        auto new_sock = std::make_shared<io::SocketChannel>(loop, std::move(client), peer);
        loop->spawn(process(loop, new_sock, service));
    });

  auto sig = io::signal(&loop, SIGINT)
      .andThen([&] (int signum) {
      FUTURES_DLOG(INFO) << "killed by " << signum;
      EventExecutor::current()->stop();
      return makeOk();
  });
  loop.spawn(std::move(sig));
  loop.spawn(std::move(f));
  loop.run();

  return 0;
}


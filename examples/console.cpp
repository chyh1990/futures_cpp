#include <futures/EventExecutor.h>
#include <futures/Timeout.h>
#include <futures/io/PipeChannel.h>

using namespace futures;

int main(int argc, char *argv[])
{
  ::fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);

  EventExecutor loop(true);
  auto in = std::make_shared<io::PipeChannel>(&loop, folly::File(fileno(stdin)), folly::File());
  auto f = in->readStream()
    .forEach([] (std::unique_ptr<folly::IOBuf> buf) {
        FUTURES_DLOG(INFO) << buf->coalesce().str();
    })
    | [] (Unit) {
      EventExecutor::current()->stop();
      return unit;
    };
  auto print = makeLoop(0, [&loop] (int i) {
      return delay(&loop, 1.0)
        >> [i] (Unit) {
          FUTURES_LOG(INFO) << "Timer: " << i;
          return makeOk(makeContinue<Unit, int>(i+1));
        };
  });
  loop.spawn(std::move(f));
  loop.spawn(std::move(print));
  loop.run();
  return 0;
}

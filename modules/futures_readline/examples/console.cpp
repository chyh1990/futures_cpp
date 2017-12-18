#include <futures_readline/Console.h>

using namespace futures;

class ExampleConsole final : public readline::Console {
public:
    ExampleConsole(EventExecutor *ev)
        : readline::Console(ev, "ex:0> ") {}

    BoxedFuture<Unit> onCommand(const std::string &line) {
        std::cout << "Get: " << line << std::endl;
        count++;
        setPrompt("ex:" + std::to_string(count) + "> ");
        return makeOk();
    }
private:
    int count = 0;
};

int main(int argc, char *argv[])
{
    EventExecutor ev(true);

#if 0
    auto rl = std::make_shared<readline::Readline>(&ev, "ex> ");
    auto f = (rl->readline()
        .forEach([] (std::string line) {
            printf("get: %s\nex> ", line.c_str());
            fflush(stdout);
        })
        | [] (Unit) {
            EventExecutor::current()->stop();
            return unit;
        })
        .error([] (folly::exception_wrapper w) {
            FUTURES_LOG(ERROR) << w.what();
        });
#endif
    // ev.spawn(std::move(f));
    auto console = std::make_shared<ExampleConsole>(&ev);
    console->start();
    ev.run();

    return 0;
}

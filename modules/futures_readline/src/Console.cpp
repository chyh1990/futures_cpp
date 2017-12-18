#include <futures_readline/Console.h>

namespace futures {
namespace readline {

Console::Console(EventExecutor *ev, const char *prompt)
    : reader_(std::make_shared<Readline>(ev, prompt))
{
}

static void printPrompt(const std::string &p) {
    printf("%s", p.c_str());
    fflush(stdout);
    Readline::notifyNewLine();
}

void Console::onError(folly::exception_wrapper err)
{
    FUTURES_LOG(FATAL) << "Unhandled error: " << err.what();
}

void Console::start() {
    auto self = shared_from_this();
    printPrompt(reader_->getPrompt());
    auto f = reader_->readline()
        .andThen([self] (std::string line) {
            return self->onCommand(line);
        })
        .forEach([self] (Unit) {
            printPrompt(self->reader_->getPrompt());
        })
        .andThen([self] (Unit) {
            return self->onEof();
        })
        .error([self] (folly::exception_wrapper w) {
            self->onError(w);
        });
    reader_->getExecutor()->spawn(std::move(f));
}

}
}

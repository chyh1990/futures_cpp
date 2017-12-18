#pragma once

#include <futures_readline/Readline.h>

namespace futures {
namespace readline {

class Console : public std::enable_shared_from_this<Console> {
public:
    Console(EventExecutor *ev, const char *prompt);

    void start();
    void setPrompt(const std::string &p) {
        reader_->setPrompt(p);
    }

    virtual BoxedFuture<Unit> onCommand(const std::string &line) = 0;
    virtual BoxedFuture<Unit> onEof() { return makeOk(); }
    virtual void onError(folly::exception_wrapper err);
private:
    Readline::Ptr reader_;
};

}
}

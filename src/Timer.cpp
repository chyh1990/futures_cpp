#include <futures/Timer.h>

namespace futures {

Poll<TimerFuture::Item> TimerFuture::poll() {
    std::error_code ec;
    switch (s_) {
        case INIT:
            new TimerIOHandler(reactor_, *CurrentTask::current_task(), after_);
            s_ = WAITING;
            break;
        case WAITING:
            s_ = TIMEOUT;
            return Poll<Item>(Async<Item>(ec));
        case TIMEOUT:
            throw InvalidPollStateException();
    }

    return Poll<Item>(not_ready);
}

}

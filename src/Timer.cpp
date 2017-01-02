#include <futures/Timer.h>

namespace futures {

Poll<TimerFuture::Item> TimerFuture::poll() {
    std::error_code ec;
    switch (s_) {
        case INIT:
            // should we update Task after each poll?
            handler_.reset(new TimerIOHandler(reactor_, *CurrentTask::current_task(), after_));
            s_ = WAITING;
            break;
        case WAITING:
            assert(handler_.get() != nullptr);
            if (!handler_->hasTimeout())
                break;
            s_ = TIMEOUT;
            handler_.reset();
            return Poll<Item>(Async<Item>(ec));
        case CANCELLED:
            return Poll<Item>(FutureCancelledException());
        case TIMEOUT:
            throw InvalidPollStateException();
    }

    return Poll<Item>(not_ready);
}

void TimerFuture::cancel() {
    if (s_ == WAITING) {
        handler_.reset();
    }
    s_ = CANCELLED;
}

}

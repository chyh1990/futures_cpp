#include <futures/Timer.h>

namespace futures {

Poll<TimerFuture::Item> TimerFuture::poll() {
    if (!timer_) {
        timer_.reset(new Timer(reactor_, after_));
        timer_->start();
    }
    switch (timer_->getState()) {
        case Timer::WAITING:
            // should we update Task after each poll?
            timer_->park();
            break;
        case Timer::CANCELLED:
            return Poll<Item>(FutureCancelledException());
        case Timer::DONE:
            return makePollReady(folly::unit);
        default:
            throw InvalidPollStateException();
    }

    return Poll<Item>(not_ready);
}

}

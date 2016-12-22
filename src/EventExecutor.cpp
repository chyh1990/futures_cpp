#include <futures/EventExecutor.h>
#include "LibevEventLoop.h"

namespace futures {

EventExecutor::EventExecutor() : pending_(0) {
    loop_ = new detail::LibAEEventLoop(1024);
}

EventExecutor::~EventExecutor() {
    delete loop_;
}

void EventExecutor::execute(std::unique_ptr<Runnable> run) {
    q_.push_back(*run.release());
}

}

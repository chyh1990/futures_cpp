#pragma once

#include <futures/io/WaitHandleBase.h>

namespace futures {
namespace io {

class WorkIOObject : public EventWatcherBase {
public:
    WorkIOObject(EventExecutor *ev)
        : ev_(ev) {
        attach(ev);
    }

    WorkIOObject() : ev_(nullptr) {}

    void attach(EventExecutor *ev) {
        ev_ = ev;
        ev_->linkWatcher(this);
    }

    void cleanup(CancelReason reason) override {
        if (ev_) {
            ev_->unlinkWatcher(this);
            ev_ = nullptr;
        }
    }
private:
    EventExecutor* ev_;
};

}
}

#pragma once

#include <stdexcept>
#include <cassert>
#include <futures/EventLoop.h>

extern "C" {
#include "libae/ae.h"
}

namespace futures {

namespace detail {

class LibAEError : public EventException {
public:
    LibAEError(const std::string& ex)
        : EventException("libae error: " + ex) {}
};

class LibAEEventLoop : public EventLoop {
public:
    LibAEEventLoop(int setsize) {
        ev_ = aeCreateEventLoop(setsize);
        if (!ev_)
            throw LibAEError("aeCreateEventLoop");
    }

    ~LibAEEventLoop() {
        aeDeleteEventLoop(ev_);
    }

    void stop() {
        aeStop(ev_);
    }

    void startLoop() {
        aeMain(ev_);
    }

    void addFileEvent(int fd, int mask, FileEventHandler *handler) {
        int ret = aeCreateFileEvent(ev_, fd, mask, fileProc, handler);
        if (ret != AE_OK)
            throw LibAEError("aeCreateFileEvent");
    }

    void deleteFileEvent(int fd, int mask) {
        aeDeleteFileEvent(ev_, fd, mask);
    }

    long long createTimeEvent(long long milliseconds, TimerEventHandler *handler) {
        long long ret = aeCreateTimeEvent(ev_, milliseconds, timeProc, handler, nullptr);
        if (ret != AE_OK)
            throw LibAEError("aeCreateTimeEvent");
        return ret;
    }

    void pollOnce() {
        aeProcessEvents(ev_, AE_ALL_EVENTS);
    }

private:
    aeEventLoop *ev_;

    static inline void fileProc(aeEventLoop *ev, int fd, void *clientData, int mask);
    static inline int timeProc(aeEventLoop *ev, long long id, void *clientData);

};

void LibAEEventLoop::fileProc(aeEventLoop *ev, int fd, void *clientData, int mask) {
    FileEventHandler *h = static_cast<FileEventHandler*>(clientData);
    assert(h != nullptr);
    (*h)(fd, mask);
}

int LibAEEventLoop::timeProc(aeEventLoop *ev, long long id, void *clientData) {
    TimerEventHandler *h = static_cast<TimerEventHandler*>(clientData);
    assert(h != nullptr);
    return (*h)(id, 0);
}

}
}

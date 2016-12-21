#pragma once

#include <stdexcept>
#include <system_error>
#include <cassert>

namespace futures {

class EventException : public std::runtime_error {
public:
    EventException(const std::string& ex)
        : std::runtime_error(ex) {}
};

class IOError : public EventException {
public:
    IOError(const std::string &ex)
        : EventException(ex) {}

    IOError(const std::error_code& ec)
        : EventException(std::to_string(ec.value()) + "-" + ec.message()) {
    }

    IOError(const std::string &what, const std::error_code& ec)
        : EventException(what + ": " + std::to_string(ec.value()) + "-" + ec.message()) {
    }
};

class FileEventHandler;
class TimerEventHandler;

class EventLoop {
public:
    virtual ~EventLoop() = default;
    virtual void stop() = 0;
    virtual void startLoop() = 0;
    virtual void addFileEvent(int fd, int mask, FileEventHandler *handler) = 0;
    virtual void deleteFileEvent(int fd, int mask) = 0;
    virtual void pollOnce() = 0;

    EventLoop() = default;
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = default;
    EventLoop& operator=(EventLoop&&) = default;
};

class EventHandler {
public:
    EventHandler(EventLoop* ev)
        : ev_(ev) {}

    EventLoop &getEventLoop() { return *ev_; }
    virtual ~EventHandler() = default;

private:
    EventLoop *ev_;
};

class FileEventHandler: public EventHandler {
public:
    FileEventHandler(EventLoop* ev)
        : EventHandler(ev) {}
    virtual void operator()(int fd, int mask) = 0;
};

class TimerEventHandler: public EventHandler {
public:
    TimerEventHandler(EventLoop* ev)
        : EventHandler(ev) {}
    virtual int operator()(long long id, int mask) = 0;
};

}

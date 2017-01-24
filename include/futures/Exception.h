#pragma once

#include <stdexcept>
#include <system_error>

namespace futures {

class InvalidPollStateException : public std::runtime_error {
public:
  InvalidPollStateException()
      : std::runtime_error("Cannot poll twice") {}
};

class InvalidChannelStateException: public std::runtime_error {
public:
  InvalidChannelStateException()
      : std::runtime_error("bad channel") {}
};

class MovedFutureException: public std::runtime_error {
public:
  MovedFutureException()
      : std::runtime_error("Cannot use moved future") {}
};

class FutureCancelledException: public std::runtime_error {
public:
  FutureCancelledException()
    : std::runtime_error("Future cancelled") {}
};

class FutureEmptySetException: public std::runtime_error {
public:
  FutureEmptySetException()
    : std::runtime_error("Future empty") {}
};

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

class DispatchException : public std::runtime_error {
public:
    DispatchException(const std::string& ex)
        : std::runtime_error(ex) {}
};



}

#pragma once

#include <stdexcept>
#include <system_error>
#include <cassert>
#include <ev++.h>

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

}

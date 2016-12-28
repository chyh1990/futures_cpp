#pragma once

#include <stdexcept>
#include <system_error>
#include <cassert>
#include <boost/intrusive/list.hpp>
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

class EventWatcherBase
{
public:
    boost::intrusive::list_member_hook<> event_hook_;

    typedef boost::intrusive::member_hook<EventWatcherBase,
            boost::intrusive::list_member_hook<>,
            &EventWatcherBase::event_hook_> MemberHookOption;

    typedef boost::intrusive::list<EventWatcherBase, MemberHookOption,
            boost::intrusive::constant_time_size<true>> EventList;

    virtual void cleanup(int reason) = 0;
    virtual ~EventWatcherBase() = default;
};

}

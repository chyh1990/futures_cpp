#pragma once

#include <stdexcept>
#include <system_error>
#include <cassert>
#include <boost/intrusive/list.hpp>
#include <ev++.h>
#include <futures/Exception.h>

namespace futures {

class EventWatcherBase
{
public:
    boost::intrusive::list_member_hook<> event_hook_;

    typedef boost::intrusive::member_hook<EventWatcherBase,
            boost::intrusive::list_member_hook<>,
            &EventWatcherBase::event_hook_> MemberHookOption;

    typedef boost::intrusive::list<EventWatcherBase, MemberHookOption,
            boost::intrusive::constant_time_size<true>> EventList;

    virtual void cleanup(CancelReason reason) = 0;
    virtual ~EventWatcherBase() = default;
};

}

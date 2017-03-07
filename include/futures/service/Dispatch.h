#pragma once

#include <futures/Async.h>
#include <futures/Core.h>

namespace futures {
namespace service {

template <typename Req, typename Resp = Req>
class IDispatcher {
public:
    virtual void dispatch(Req&& in) = 0;
    virtual void dispatchErr(folly::exception_wrapper err) = 0;
    virtual bool has_in_flight() = 0;
    virtual Poll<Optional<Resp>> poll() = 0;
    virtual ~IDispatcher() = default;
};

}
}

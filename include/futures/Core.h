#pragma once

#include <futures/core/Memory.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>
#include <futures/core/ApplyTuple.h>
#include <futures/core/MoveWrapper.h>
// #include <futures/core/FBString.h>

#ifndef NDEBUG
#define FUTURES_ENABLE_DEBUG_PRINT 1
#endif

#include <iostream>
#include <sstream>

namespace futures {

template <typename T>
struct isTry : std::false_type {};

template <typename T>
struct isTry<folly::Try<T>> : std::true_type {};

namespace detail {

template<typename F, typename... Args>
using resultOf = decltype(std::declval<F>()(std::declval<Args>()...));

template <typename...>
struct ArgType;

template <typename Arg, typename... Args>
struct ArgType<Arg, Args...> {
  typedef Arg FirstArg;
};

template <>
struct ArgType<> {
  typedef void FirstArg;
};

}

namespace debug {

struct nullstream {
	nullstream() {}
};

template <typename T>
nullstream operator<<(nullstream o, const T& x) { return o;}

class LogMessage{
	std::string level;
	std::ostream &ofs;
	public:
		LogMessage(const std::string &l)
			:level(l), ofs(std::cerr){
			stream() << "[" << level << "]\t";
		}
		LogMessage(std::ostream &o)
			:level("ERROR"), ofs(o){
			stream() << "[" << level << "]\t";
		}
		inline std::ostream &stream(){
			return ofs;
		}
		~LogMessage() {
			stream() << std::endl;
		}
};
}

#define 	FUTURES_LOG(type)   futures::debug::LogMessage(#type).stream()
#if FUTURES_ENABLE_DEBUG_PRINT
#define 	FUTURES_DLOG(type)   futures::debug::LogMessage(#type).stream()
#else
#define     FUTURES_DLOG(type)   futures::debug::nullstream()
#endif

#define     FUTURES_CHECK(x) if(x) {} else FUTURES_LOG(ERROR) << #x
#define     FUTURES_DCHECK(x) if(x) {} else FUTURES_DLOG(ERROR) << #x

#define 	FUTURES_CHECK_EQ(x, y)   FUTURES_CHECK((x) == (y))
#define 	FUTURES_CHECK_LT(x, y)   FUTURES_CHECK((x) < (y))
#define 	FUTURES_CHECK_GT(x, y)   FUTURES_CHECK((x) > (y))
#define 	FUTURES_CHECK_LE(x, y)   FUTURES_CHECK((x) <= (y))
#define 	FUTURES_CHECK_GE(x, y)   FUTURES_CHECK((x) >= (y))
#define 	FUTURES_CHECK_NE(x, y)   FUTURES_CHECK((x) != (y))

#define 	FUTURES_DCHECK_EQ(x, y)   FUTURES_DCHECK((x) == (y))
#define 	FUTURES_DCHECK_LT(x, y)   FUTURES_DCHECK((x) < (y))
#define 	FUTURES_DCHECK_GT(x, y)   FUTURES_DCHECK((x) > (y))
#define 	FUTURES_DCHECK_LE(x, y)   FUTURES_DCHECK((x) <= (y))
#define 	FUTURES_DCHECK_GE(x, y)   FUTURES_DCHECK((x) >= (y))
#define 	FUTURES_DCHECK_NE(x, y)   FUTURES_DCHECK((x) != (y))

}

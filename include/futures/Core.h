#pragma once

#include <futures/core/Memory.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>
#include <futures/core/ApplyTuple.h>
#include <futures/core/MoveWrapper.h>
#include <futures/core/variant/variant.hpp>
// #include <futures/core/FBString.h>

#ifndef NDEBUG
#define FUTURES_ENABLE_DEBUG_PRINT 1
#endif

#include <iostream>
#include <sstream>

namespace futures {

using folly::Try;
using folly::Optional;
using folly::Unit;
using folly::unit;
using folly::none;

template <typename... Args>
using Variant = mapbox::util::variant<Args...>;

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
  LogMessage(const std::string &l, const char *file, int line)
    :level(l), ofs(std::cerr){
      stream() << "[" << level << "] " << basename(file) << ":" << line << " " ;
    }
  LogMessage(std::ostream &o)
    :level("ERROR"), ofs(o){
      stream() << "[" << level << "] ";
    }
  inline std::ostream &stream(){
    return ofs;
  }
  ~LogMessage() {
    stream() << std::endl;
    if (level == "FATAL") abort();
  }

private:
  std::string basename(const std::string &full) {
    std::size_t found = full.find_last_of("/\\");
    if (found == std::string::npos) {
      return full;
    } else {
      return full.substr(found + 1);
    }
  }
};

}

#define 	FUTURES_LOG(type)   futures::debug::LogMessage(#type, __FILE__, __LINE__).stream()
#if FUTURES_ENABLE_DEBUG_PRINT
#define 	FUTURES_DLOG(type)   FUTURES_LOG(type)
#else
#define     FUTURES_DLOG(type)   futures::debug::nullstream()
#endif

#define     FUTURES_CHECK(x) if(x) {} else FUTURES_LOG(FATAL) << #x << ' '
#define     FUTURES_DCHECK(x) if(x) {} else FUTURES_DLOG(FATAL) << #x << ' '

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

#pragma once

#include <futures/Exception.h>

extern "C" {
extern const char* zerror(int c);
}

namespace futures {
namespace zookeeper {

class ZookeeperException : public std::runtime_error {
public:
    ZookeeperException(const std::string &err, int code)
        : std::runtime_error(err + ": "
                + zerror(code) + " (" + std::to_string(code) + ")"),
        code_(code) {}

    int getErrorCode() const { return code_; }
    const char *getErrorString() { return zerror(code_); }
private:
    int code_;
};

class SystemErrorException : public ZookeeperException {
public:
    SystemErrorException(int code);
};

class ApiErrorException : public ZookeeperException {
public:
    ApiErrorException(int code);
};

#define __DEF_ZEX(name) \
    class name##Exception : public ApiErrorException { \
    public: \
        name##Exception(); \
    }

__DEF_ZEX(NoNode);
__DEF_ZEX(NoAuth);
__DEF_ZEX(BadVersion);
__DEF_ZEX(NoChildrenForEphemerals);
__DEF_ZEX(NodeExists);
__DEF_ZEX(NotEmpty);

#undef __DEF_ZEX


}
}

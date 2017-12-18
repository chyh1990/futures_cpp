#pragma once

#include <stdexcept>

namespace futures {
namespace mysql {

class MySqlError {
public:
    MySqlError(int code, const char* s)
        : code_(code), str_(s) {}
    MySqlError() : code_(0) {}

    bool good() const {
        return code_ == 0;
    }

    std::string str() const {
        return std::to_string(code_) + ": " + str_;
    }
private:
    int code_;
    std::string str_;
};

class MySqlException : public std::runtime_error {
public:
    explicit MySqlException(const std::string &err)
        : std::runtime_error(err) {}

    MySqlException(int code, const std::string sqlerr)
        : std::runtime_error(sqlerr) {}

    explicit MySqlException(const MySqlError& err)
        : std::runtime_error(err.str()) {}
private:
};

}
}

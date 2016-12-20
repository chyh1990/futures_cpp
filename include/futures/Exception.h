#pragma once

#include <stdexcept>

namespace futures {

class InvalidPollStateException : public std::runtime_error {
 public:
  InvalidPollStateException()
      : std::runtime_error("Cannot poll twice") {}
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

}

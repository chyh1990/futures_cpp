#pragma once

#include <cassert>

#include <futures/Async.h>
#include <futures/Task.h>

namespace futures {

template <typename T>
class UnparkMutex {
public:
    enum {
        kWaiting = 0,
        kPolling = 1,
        kRepoll = 2,
        kComplete = 3,
    };

    UnparkMutex()
        : status_(kWaiting) {
    }

    ~UnparkMutex() {
    }

    void start_poll() {
        status_.store(kPolling, std::memory_order_seq_cst);
    }

    Optional<T> wait(T &&data) {
        data_ = std::move(data);

        int expected = kPolling;
        if (status_.compare_exchange_strong(expected, kWaiting)) {
            // no unparks came in while we were running
            return folly::None();
        } else {
            // guaranteed to be in REPOLL state;
            assert(expected == kRepoll);
            status_.store(kPolling);
            return std::move(data_);
        }
    }

    void complete() {
        status_.store(kComplete);
    }

    // true indicated that kPolling is entered;
    Optional<T> notify() {
        int status = status_.load(std::memory_order_seq_cst);
        int expected;
        while (true) {
            switch (status) {
                // The task is idle, so try to run it immediately.
            case kWaiting:
                expected = kWaiting;
                if (status_.compare_exchange_strong(expected, kPolling)) {
                    return std::move(data_);
                } else {
                    status = expected;
                }
                break;
            case kPolling:
                expected = kPolling;
                if (status_.compare_exchange_strong(expected, kRepoll)) {
                    return folly::None();
                } else {
                    status = expected;
                }
                break;
            // complete
            default:
                return folly::None();
            }
        }
    }

private:
    std::atomic_int status_;
    Optional<T> data_;
};

}

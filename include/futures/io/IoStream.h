#pragma once

#include <futures/core/IOBufQueue.h>
#include <futures/io/Io.h>
#include <futures/Stream.h>

#include <futures/EventExecutor.h>

namespace futures {
namespace io {

class BytesReadStream : public StreamBase<BytesReadStream, folly::IOBufQueue*> {
public:
    using Item = folly::IOBufQueue*;

    Poll<Optional<Item>> poll() override {
        while (true) {
            auto space = q_.preallocate(2048, 4096);
            std::error_code ec;
            ssize_t len = io_->read(space.first, space.second, ec);
            if (ec == std::make_error_code(std::errc::connection_aborted)) {
                return makePollReady(Optional<Item>());
            } else if (ec) {
                return Poll<Optional<Item>>(IOError("recv", ec));
            } else if (len == 0) {
                if (io_->poll_read().isReady()) continue;
                return Poll<Optional<Item>>(not_ready);
            } else {
                q_.postallocate(len);
                return makePollReady(Optional<Item>(&q_));
            }
        }
    }

    BytesReadStream(std::unique_ptr<Io> io)
        : io_(std::move(io)) {
    }

private:
    std::unique_ptr<Io> io_;
    folly::IOBufQueue q_;
};

}
}



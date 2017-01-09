#pragma once

#include <system_error>
#include <futures/Core.h>
#include <futures/Async.h>
#include <futures/core/IOBuf.h>

namespace futures {
namespace io {

template <typename U, typename V>
class Codec {
public:
    using In = U;
    using Out = V;

    Try<Optional<In>> decode(folly::IOBuf *buf);
    Try<Optional<Out>> encode(folly::IOBuf *buf);
};

class Readable {
public:
    virtual ssize_t read(folly::IOBuf *buf, size_t len, std::error_code &ec) = 0;
};

class Writable {
public:
    virtual ssize_t write(const folly::IOBuf &buf, size_t len, std::error_code &ec) = 0;
};

class Io : public Readable, public Writable {
public:
    virtual Async<folly::Unit> poll_read() {
        return Async<folly::Unit>(folly::unit);
    }

    virtual Async<folly::Unit> poll_write() {
        return Async<folly::Unit>(folly::unit);
    }

    virtual ~Io() = default;
};


}
}

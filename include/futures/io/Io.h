#pragma once

#include <system_error>
#include <futures/Core.h>
#include <futures/Async.h>
#include <futures/Exception.h>
#include <futures/core/IOBuf.h>

namespace futures {
namespace io {

template <class Derived, typename U, typename V>
class Codec {
public:
    using In = U;
    using Out = V;

    Try<Optional<In>> decode(std::unique_ptr<folly::IOBuf> &buf) {
        assert(0 && "unimpl");
    }

    Try<In> decode_eof(std::unique_ptr<folly::IOBuf> &buf) {
        auto v = static_cast<Derived*>(this)->decode(buf);
        if (v.hasException())
            return Try<In>(v.exception());
        if (v->hasValue()) {
            return Try<In>(folly::moveFromTry(v).value());
        } else {
            return Try<In>(IOError("eof"));
        }
    }

    Try<folly::Unit> encode(const Out& out,
            std::unique_ptr<folly::IOBuf> &buf) {
        assert(0 && "unimpl");
    }
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

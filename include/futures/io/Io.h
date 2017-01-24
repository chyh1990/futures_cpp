#pragma once

#include <system_error>
#include <futures/Core.h>
#include <futures/Async.h>
#include <futures/Exception.h>
#include <futures/core/IOBuf.h>
#include <futures/core/IOBufQueue.h>

namespace futures {
namespace io {

template <class Derived, typename T>
class DecoderBase {
public:
    using Out = T;

    Try<Optional<Out>> decode(folly::IOBufQueue &buf) {
        assert(0 && "unimpl");
    }

    Try<Out> decode_eof(folly::IOBufQueue &buf) {
        auto v = static_cast<Derived*>(this)->decode(buf);
        if (v.hasException())
            return Try<Out>(v.exception());
        if (v->hasValue()) {
            return Try<Out>(folly::moveFromTry(v).value());
        } else {
            return Try<Out>(IOError("eof"));
        }
    }

};

template <class Derived, typename T>
class EncoderBase {
public:
    using Out = T;

    Try<void> encode(Out&& out,
            folly::IOBufQueue &buf) {
        assert(0 && "unimpl");
    }
};

class Readable {
public:
    virtual ssize_t read(void *buf, size_t len, std::error_code &ec) = 0;
};

class Writable {
public:
    virtual ssize_t write(const void *buf, size_t len, std::error_code &ec) {
        iovec vec[1];
        vec[0].iov_base = const_cast<void*>(buf);
        vec[0].iov_len = len;
        return writev(vec, 1, ec);
    }

    virtual ssize_t writev(const iovec *buf, size_t veclen, std::error_code &ec) = 0;
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

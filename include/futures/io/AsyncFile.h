#pragma once

#include <futures/core/File.h>
#include <futures/Future.h>
#include <futures/core/IOBuf.h>
#include <futures/io/WaitHandleBase.h>

namespace futures {
namespace io {

class AsyncFile : public std::enable_shared_from_this<AsyncFile> {
public:
    using Ptr = std::shared_ptr<AsyncFile>;
    using buf_ptr = std::unique_ptr<folly::IOBuf>;

    AsyncFile(folly::File file)
        : file_(std::move(file)) {}

    AsyncFile() {}

    bool isValid() const { return (bool)file_; }

    void openSync(const std::string &path, int flags, mode_t mode = 0644);
    BoxedFuture<Unit> open(const std::string &path, int flags, mode_t mode = 0644);

    ssize_t readSync(void *buf, size_t count);
    BoxedFuture<buf_ptr> read(size_t count);
    BoxedFuture<buf_ptr> read(buf_ptr);

    ssize_t writeSync(const void *buf, size_t count);
    // ssize_t writeIovSync(struct iovec *v);

    BoxedFuture<ssize_t> write(buf_ptr buf);

    void fsyncSync(bool data_only = false);
    BoxedFuture<Unit> fsync(bool data_only = false);

    void closeSync();
    BoxedFuture<Unit> close();

    int fd() const { return file_.fd(); }
private:
    folly::File file_;
};

}
};

#include <futures/io/AsyncFile.h>
#include <futures/CpuPoolExecutor.h>

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace futures {
namespace io {

class FileIOPool {
public:
    static CpuPoolExecutor& getExecutor() {
        std::call_once(flag_, [] () {
            long ncpu = sysconf(_SC_NPROCESSORS_CONF);
            if (ncpu <= 0) ncpu = 1;
            FileIOPool::executor_.reset(new CpuPoolExecutor(ncpu));
            FUTURES_DLOG(INFO) << "FileIOPool created: "
              << ncpu << " threads";
        });
        return *executor_;
    }

    static void init() {
      getExecutor();
    }

private:
    FileIOPool() = default;
    FileIOPool(const FileIOPool&) = delete;
    FileIOPool& operator=(const FileIOPool&) = delete;

    static std::once_flag flag_;
    static std::unique_ptr<CpuPoolExecutor> executor_;
};

std::once_flag FileIOPool::flag_;
std::unique_ptr<CpuPoolExecutor> FileIOPool::executor_;

#ifdef __linux__
static void SetCurrentThreadName(const std::string &name) {
  // avoid change main thread name
  if (getpid() == syscall(SYS_gettid)) return;
  prctl(PR_SET_NAME, name.substr(0, 16).c_str(), NULL, NULL, NULL);
}
#else
static void SetCurrentThreadName(const std::string &name) {}
#endif

static void throwSystemError(const std::string &s) {
  throw std::system_error(errno, std::system_category(), s);
}

void AsyncFile::openSync(const std::string &path, int flags, mode_t mode)
{
    file_ = folly::File(path.c_str(), flags, mode);
}

BoxedFuture<Unit> AsyncFile::open(const std::string &path, int flags, mode_t mode) {
    auto self = shared_from_this();
    return FileIOPool::getExecutor().spawn_fn([self, path, flags, mode] () {
        self->openSync(path, flags, mode);
        return unit;
    });
}


ssize_t AsyncFile::readSync(void *buf, size_t count)
{
again:
    ssize_t size = ::read(file_.fd(), buf, count);
    if (size < 0) {
        if (errno == EINTR) goto again;
        throwSystemError("read");
    }
    return size;
}

BoxedFuture<AsyncFile::buf_ptr> AsyncFile::read(size_t count)
{
  auto buf = folly::IOBuf::create(count);
  return read(std::move(buf));
}

BoxedFuture<AsyncFile::buf_ptr> AsyncFile::read(buf_ptr buf)
{
    auto self = shared_from_this();
    auto m = folly::makeMoveWrapper(buf);
    return FileIOPool::getExecutor().spawn_fn([self, m] () {
        auto buf = m.move();
        ssize_t size = self->readSync(buf->writableTail(), buf->tailroom());
        buf->append(size);
        return buf;
    });
}


ssize_t AsyncFile::writeSync(const void *buf, size_t count)
{
again:
    ssize_t size = ::write(file_.fd(), buf, count);
    if (size < 0) {
        if (errno == EINTR) goto again;
        throwSystemError("write");
    }
    return size;
}

BoxedFuture<ssize_t> AsyncFile::write(buf_ptr buf)
{
    auto self = shared_from_this();
    auto m = folly::makeMoveWrapper(buf);
    return FileIOPool::getExecutor().spawn_fn([self, m] () {
        auto buf = m.move();
        return self->writeSync(buf->data(), buf->length());
    });
}

void AsyncFile::fsyncSync(bool data_only) {
    if (data_only) {
#if __linux__
        int rc = ::fdatasync(file_.fd());
#else
        int rc = ::fsync(file_.fd());
#endif
        if (rc) throwSystemError("fdatasync");
    } else {
        int rc = ::fsync(file_.fd());
        if (rc) throwSystemError("fsync");
    }
}

BoxedFuture<Unit> AsyncFile::fsync(bool data_only) {
    auto self = shared_from_this();
    return FileIOPool::getExecutor().spawn_fn([self, data_only] () {
        self->fsyncSync(data_only);
        return unit;
    });
}

void AsyncFile::closeSync() {
    file_.close();
}

BoxedFuture<Unit> AsyncFile::close() {
    auto self = shared_from_this();
    return FileIOPool::getExecutor().spawn_fn([self] () {
        self->closeSync();
        return unit;
    });
}

}
}

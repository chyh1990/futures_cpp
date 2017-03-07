#pragma once

#include <futures/io/Channel.h>
#include <futures/core/File.h>

namespace futures {
namespace io {

class ReadStream;
class WriteFuture;

class PipeChannel : public Channel,
    public std::enable_shared_from_this<PipeChannel>
{
protected:
    enum State {
        INITED,
        CLOSED,
    };

    enum ReadResult {
        READ_EOF = 0,
        READ_WOULDBLOCK = -1,
        READ_ERROR = -2,
    };

public:
    using Ptr = std::shared_ptr<PipeChannel>;

    PipeChannel(EventExecutor *ev, folly::File rfd, folly::File wfd);

    bool good() const override { return s_ != CLOSED; }

    io::intrusive_ptr<WriterCompletionToken> doWrite(std::unique_ptr<WriterCompletionToken> p) override;
    io::intrusive_ptr<ReaderCompletionToken> doRead(std::unique_ptr<ReaderCompletionToken> p) override;

    // futures API
    ReadStream readStream();
    WriteFuture write(std::unique_ptr<folly::IOBuf> buf);

    void shutdownWrite() override {
        shutdownWriteNow();
    }

    void shutdownWriteNow() override {
        failAllWrites();
        if (wfd_) {
            s_ = CLOSED;
            wfd_.close();
            wio_.stop();
        }
    }
private:
    State s_ = INITED;
    folly::File rfd_;
    folly::File wfd_;
    ev::io rio_;
    ev::io wio_;

    void onEvent(ev::io& watcher, int revent);
    ssize_t handleRead(ReaderCompletionToken *tok, std::error_code &ec);
    void handleWrite() {}
    ssize_t performRead(void* buf, size_t buflen, std::error_code &ec);
    ssize_t performWrite(
            const iovec* vec,
            size_t count,
            size_t* countWritten,
            size_t* partialWritten,
            std::error_code &ec);

    void failAllWrites() {
        auto &writer = getPending(IOObject::OpWrite);
        while (!writer.empty()) {
            auto p = static_cast<WriterCompletionToken*>(&writer.front());
            p->writeError(std::make_error_code(std::errc::broken_pipe));
        }
    }

};

}
}

#include <futures/io/ChannelFutures.h>

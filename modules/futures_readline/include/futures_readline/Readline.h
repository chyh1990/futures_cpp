#pragma once

#include <futures/EventExecutor.h>
#include <futures/io/WaitHandleBase.h>
#include <futures/Stream.h>

namespace futures {
namespace readline {

class ReadlineToken : public io::CompletionToken {
public:
   ReadlineToken()
      : io::CompletionToken(io::IOObject::OpRead) {}

   void onCancel(CancelReason r) {}
};

class ReadlineStream;

class Readline
   : public io::IOObject, public std::enable_shared_from_this<Readline>
{
public:
    using Ptr = std::shared_ptr<Readline>;

    static void setNonblockPipe(int fd);
    static void setAlreadyPrompt(bool v);
    static void notifyNewLine();

    Readline(EventExecutor *ev, const std::string &prompt);

    ~Readline();

    bool isEof() const;
    const std::string &getPrompt() const { return prompt_; }

    io::intrusive_ptr<ReadlineToken> doReadline();
    ReadlineStream readline();

    void setPrompt(const std::string &prompt);
private:
    std::string prompt_;
    ev::io io_;

    void onEvent(ev::io &watcher, int revents);
    static void onNewLine(char *line);

    void notify(bool eof);
};

class ReadlineStream : public StreamBase<ReadlineStream, std::string> {
public:
   using Item = std::string;

   ReadlineStream(Readline::Ptr ctx) : ctx_(ctx) {}

   Poll<Optional<Item>> poll() override;

private:
   Readline::Ptr ctx_;
   io::intrusive_ptr<ReadlineToken> tok_;
};

}
}

#include <deque>
#include <futures_readline/Readline.h>
#include <futures/io/PipeChannel.h>
#include <readline/readline.h>
#include <readline/history.h>

namespace futures {
namespace readline {

static std::deque<std::string> gLines;
static bool gEof = false;

static std::weak_ptr<Readline> gReadline;

void Readline::setNonblockPipe(int fd) {
    if (fcntl(fileno(stdin), F_SETFL, O_NONBLOCK))
        throw IOError("fcntl", std::error_code(errno, std::system_category()));
}

void Readline::setAlreadyPrompt(bool v) {
    rl_already_prompted = v ? 1 : 0;
}

void Readline::notifyNewLine() {
    rl_on_new_line_with_prompt();
}

Readline::Readline(EventExecutor *ev, const std::string& prompt)
    : io::IOObject(ev), prompt_(prompt), io_(ev->getLoop())
{
    if (gReadline.lock())
        throw std::logic_error("Readline already initialized.");
    setAlreadyPrompt(true);
    setNonblockPipe(fileno(stdin));
    rl_callback_handler_install(prompt.c_str(),
            (rl_vcpfunc_t*)Readline::onNewLine);
    io_.set<Readline, &Readline::onEvent>(this);
    io_.set(fileno(stdin), ev::READ);
}

void Readline::onEvent(ev::io &watcher, int revents) {
    if (revents & ev::READ)
        rl_callback_read_char();
    if (gEof) io_.stop();
}

void Readline::onNewLine(char *line) {
    auto inst = gReadline.lock();
    if(!line){
        // Ctrl-D will allow us to exit nicely
        FUTURES_DLOG(INFO) << "readline eof";
        gEof = true;
        if (inst) inst->notify(true);
    }else{
        if(*line!=0){
            // If line wasn't empty, store it so that uparrow retrieves it
            add_history(line);
        }
        // printf("Your input was:\n%s\n", line);
        gLines.push_back(line);
        free(line);
        if (inst) inst->notify(false);
    }
}

bool Readline::isEof() const { return gEof; }

void Readline::setPrompt(const std::string &prompt) {
    prompt_ = prompt;
    rl_set_prompt(prompt.c_str());
}

void Readline::notify(bool eof) {
    auto &readers = getPending(io::IOObject::OpRead);
    if (readers.empty()) return;
    auto p = static_cast<ReadlineToken*>(&readers.front());
    if (eof) {
        p->notifyDone();
    } else {
        p->notify();
    }
}

Readline::~Readline() {
    rl_callback_handler_remove();
}

io::intrusive_ptr<ReadlineToken> Readline::doReadline() {
    gReadline = shared_from_this();
    auto p = io::intrusive_ptr<ReadlineToken>(new ReadlineToken());
    if (gEof) {
        p->notifyDone();
    } else {
        io_.start();
        p->attach(this);
    }
    return p;
}

ReadlineStream Readline::readline() {
    return ReadlineStream(shared_from_this());
}

Poll<Optional<std::string>> ReadlineStream::poll() {
    if (!tok_) tok_ = ctx_->doReadline();
    if (!gLines.empty()) {
        auto v = std::move(gLines.front());
        gLines.pop_front();
        return makeStreamReady(std::move(v));
    }
    switch (tok_->getState()) {
    case ReadlineToken::STARTED:
        tok_->park();
        return Poll<Optional<Item>>(not_ready);
    case ReadlineToken::DONE:
        return Poll<Optional<Item>>(Optional<Item>());
    case ReadlineToken::CANCELLED:
        return Poll<Optional<Item>>(FutureCancelledException());
    };
}

}
}

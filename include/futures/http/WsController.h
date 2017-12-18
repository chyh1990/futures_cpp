#pragma once

#include <regex>
#include <futures/http/WsCodec.h>
#include <futures/service/Service.h>
#include <futures/io/AsyncSocket.h>
#include <futures/io/AsyncServerSocket.h>

namespace futures {
namespace websocket {

class Connection;
class WsServer;

class Handler {
public:
    using conn_ptr = std::shared_ptr<Connection>;
    virtual ~Handler() = default;
    virtual void onError(conn_ptr conn) {}
    virtual void onConnect(conn_ptr conn) {}

    virtual BoxedFuture<Unit> onText(conn_ptr conn, const std::string &text) = 0;
    virtual BoxedFuture<Unit> onBinary(conn_ptr conn, const std::string &text) = 0;

    virtual void onClose(conn_ptr conn) {  }
};

class Notifier {
public:
    void notify() {
        ready_ = true;
        if (task_) task_->unpark();
        task_.clear();
    }

    void park() {
        task_ = CurrentTask::park();
    }

    void reset() {
        ready_ = false;
    }

    bool isReady() const {
        return ready_;
    }

private:
    bool ready_ = false;
    Optional<Task> task_;
};

class DataFlushFuture : public FutureBase<DataFlushFuture, Unit> {
public:
    using Item = Unit;
    using Sink = io::FramedSink<websocket::DataFrame>;

    DataFlushFuture(std::shared_ptr<Connection> conn)
        : conn_(conn) {
    }

    Poll<Unit> poll() override;

private:
    std::shared_ptr<Connection> conn_;
};

// XXX handle force close
class Connection : public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;
    enum State {
        HANDSHAKING,
        CONNECTED,
        CLOSED,
    };

    using Stream = io::FramedStream<websocket::DataFrame>;
    using Sink = io::FramedSink<websocket::DataFrame>;

    Connection(std::shared_ptr<WsServer> server, io::SocketChannel::Ptr sock)
        : server_(server), sock_(sock),
        stream_(sock_, std::make_shared<RFC6455Decoder>()),
        sink_(sock_, std::make_shared<RFC6455Encoder>()) {
    }

    BoxedFuture<Unit> process() {
        auto self = shared_from_this();
        sock_->getExecutor()->spawn(DataFlushFuture(self));
        return stream_
            .andThen([self, this] (DataFrame frame) {
            switch (s_) {
            case HANDSHAKING:
                if (frame.getType() == DataFrame::HANDSHAKE) {
                    FUTURES_DLOG(INFO) << "url: " << *frame.getHandshake();
                    if (matchHandler(frame.getHandshake()->path)) {
                        sink_.startSend(DataFrame::buildHandshakeResponse(*frame.getHandshake()));
                        flush();
                        s_ = CONNECTED;
                        handler_->onConnect(self);
                        return makeOk().boxed();
                    } else {
                        http::HttpFrame frame;
                        frame.http_errno = 404;
                        frame.body.append(folly::IOBuf::wrapBuffer("Not Found", 9));
                        sink_.startSend(DataFrame(DataFrame::HANDSHAKE_RESPONSE, std::move(frame)));
                        close();
                        return makeOk().boxed();
                    }
                } else {
                    throw IOError("invalid request");
                }
                break;
            case CONNECTED:
                FUTURES_DLOG(INFO) << "frame: " << frame.getType()
                     << ", " << frame.getData();
                if (frame.getType() == DataFrame::CLOSE) {
                    if (handler_)
                        handler_->onClose(self);
                    close();
                    return makeOk().boxed();
                } else if (frame.getType() == DataFrame::TEXT) {
                    if (handler_) return handler_->onText(self, frame.getData());
                } else if (frame.getType() == DataFrame::BINARY) {
                    if (handler_) return handler_->onBinary(self, frame.getData());
                } else if (frame.getType() == DataFrame::PING) {
                    sendPong();
                    return makeOk().boxed();
                }
                return makeOk().boxed();
            case CLOSED:
            default:
                throw FutureCancelledException();
            }
        }).drop()
        .then([self, this] (Try<Unit> err) {
            if (err.hasException()) {
                FUTURES_DLOG(ERROR) << err.exception().what();
                if (s_ != CLOSED) {
                    if (handler_) handler_->onError(self);
                }
            } else {
                if (s_ != CLOSED) {
                    if (handler_) handler_->onClose(self);
                }
            }
            close();
            return makeOk();
        });
    }

    bool good() const { return s_ == CONNECTED && sock_->good(); }

    void send(DataFrame &&frame) {
        sink_.startSend(std::move(frame)).throwIfFailed();
        flush();
    }

    void sendText(const std::string &text) {
        return send(DataFrame(DataFrame::TEXT, text));
    }

    void close(int status, const std::string &reason = "") {
        if (s_ == CLOSED) return;
        return send(DataFrame(DataFrame::CLOSE, status, reason));
    }

    io::SocketChannel* getTransport() {
        return sock_.get();
    }

private:
    std::weak_ptr<WsServer> server_;
    io::SocketChannel::Ptr sock_;
    Stream stream_;
    Sink sink_;
    std::shared_ptr<Handler> handler_;
    std::smatch matches_;

    State s_ = HANDSHAKING;
    Notifier cv_;

    bool matchHandler(const std::string &url);

    void sendPong() {
        send(DataFrame(DataFrame::PONG, ""));
    }

    void flush() {
        cv_.notify();
    }

    void close() {
        if (s_ != CLOSED) {
            sock_->shutdownWrite();
            s_ = CLOSED;
        }
        cv_.notify();
    }

    friend DataFlushFuture;
};

Poll<Unit> DataFlushFuture::poll() {
    auto r = conn_->sink_.pollComplete();
    if (r.hasException()) {
        FUTURES_LOG(ERROR) << "write error: " << r.exception().what();
        return Poll<Unit>(unit);
    }
    if (!r->isReady())
        return r;
    if (conn_->s_ == Connection::CLOSED)
        return Poll<Unit>(unit);
    conn_->cv_.park();
    return Poll<Unit>(not_ready);
}


class WsServer : public std::enable_shared_from_this<WsServer>
{
public:
    WsServer(EventExecutor *ev, const folly::SocketAddress &addr)
        : sock_(std::make_shared<io::AsyncServerSocket>(ev, addr)) {
    }

    void start() {
        auto self = shared_from_this();
        auto f = sock_->accept()
            .forEach2([self, this] (tcp::Socket client, folly::SocketAddress peer) {
                auto loop = self->sock_->getExecutor();
                auto sock = std::make_shared<io::SocketChannel>(loop, std::move(client), peer);
                auto conn = std::make_shared<Connection>(self, sock);
                // conn->setHandler(nullptr);
                loop->spawn(conn->process());
            });
        sock_->getExecutor()->spawn(std::move(f));
    }

    void addRoute(const std::string& pattern, std::shared_ptr<Handler> handler) {
        resource_[pattern] = handler;
    }

private:
    io::AsyncServerSocket::Ptr sock_;
    class regex_orderable : public std::regex {
        std::string str;
        public:
        regex_orderable(const char *regex_cstr) : std::regex(regex_cstr), str(regex_cstr) {}
        regex_orderable(const std::string &regex_str)
            : std::regex(regex_str), str(regex_str) {}
        bool operator<(const regex_orderable &rhs) const {
            return str<rhs.str;
        }
    };

    std::map<regex_orderable, std::shared_ptr<Handler>> resource_;
    friend class Connection;

};

bool Connection::matchHandler(const std::string &url) {
    auto server = server_.lock();
    if (!server) return false;
    for (auto &e: server->resource_) {
        if(std::regex_match(url, matches_, e.first)) {
            handler_ = e.second;
            return true;
        }
    }
    return false;
}

}
}

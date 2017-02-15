#pragma once

#include <futures/TcpStream.h>
#include <futures/io/WaitHandleBase.h>
#include <futures/core/SocketAddress.h>
#include <deque>

namespace futures {
namespace io {

class AcceptStream;

class AsyncServerSocket
    : public IOObject,
      public std::enable_shared_from_this<AsyncServerSocket> {
public:
    using Ptr = std::shared_ptr<AsyncServerSocket>;

    AsyncServerSocket(EventExecutor *ev, const folly::SocketAddress &bind)
        : IOObject(ev), rio_(ev->getLoop()) {
        std::error_code ec;
        socket_.tcpServer(bind.getIPAddress().asV4().str(), bind.getPort(), 32, ec);
        if (ec) throw IOError("bind", ec);
        rio_.set<AsyncServerSocket, &AsyncServerSocket::onEvent>(this);
        rio_.set(socket_.fd(), ev::READ);
    }

    struct AcceptCompletionToken : public io::CompletionToken {
        std::error_code ec;
        using Item = std::tuple<tcp::Socket, folly::SocketAddress>;
        std::deque<Item> sock_;

        AcceptCompletionToken()
            : io::CompletionToken(IOObject::OpRead) {
        }

        void onCancel(CancelReason r) override {
        }

        Poll<Optional<Item>> pollStream() {
            if (!sock_.empty()) {
                auto v = std::move(sock_.front());
                sock_.pop_front();
                return makePollReady(Optional<Item>(std::move(v)));
            }
            switch (getState()) {
            case STARTED:
                park();
                return Poll<Optional<Item>>(not_ready);
            case DONE:
                if (ec) {
                    return Poll<Optional<Item>>(IOError("accept", ec));
                } else {
                    return Poll<Optional<Item>>(Optional<Item>());
                }
            case CANCELLED:
                return Poll<Optional<Item>>(FutureCancelledException());
            }
        }

        void append(Item&& item) {
            sock_.push_back(std::move(item));
        }
    protected:
        ~AcceptCompletionToken() {
            cleanup(CancelReason::UserCancel);
        }
    };

    io::intrusive_ptr<AcceptCompletionToken> doAccept() {
        io::intrusive_ptr<AcceptCompletionToken> tok(new AcceptCompletionToken());
        if (closed_) {
            tok->ec = std::make_error_code(std::errc::connection_aborted);
            tok->notifyDone();
        } else {
            tok->attach(this);
            rio_.start();
        }
        return tok;
    }

    void forceClose() {
        rio_.stop();
        socket_.close();
    }

    inline AcceptStream accept();

    void onCancel(CancelReason reason) override {
        FUTURES_DLOG(INFO) << "Cancelling";
    }

private:
    tcp::Socket socket_;
    ev::io rio_;
    bool closed_ = false;

    void onEvent(ev::io& watcher, int revent) {
        if (revent & ev::READ) {
            auto &list = getPending(IOObject::OpRead);
            if (list.empty()) {
                rio_.stop();
            } else {
                folly::SocketAddress addr;
                std::error_code ec;
                auto p = static_cast<AcceptCompletionToken*>(&list.front());
                while (true) {
                    tcp::Socket s = socket_.accept(ec, &addr);
                    if (ec) {
                        p->ec = ec;
                        p->notifyDone();
                        forceClose();
                        break;
                    } else if (!s.isValid()) {
                        // would block
                        break;
                    } else {
                        p->append(std::make_tuple(std::move(s), addr));
                        p->notify();
                    }
                }
            }
        }
    }

};

using AcceptItem = std::tuple<tcp::Socket, folly::SocketAddress>;
class AcceptStream : public StreamBase<AcceptStream, AcceptItem> {
public:
    using Item = AcceptItem;

    AcceptStream(AsyncServerSocket::Ptr sock)
        : sock_(sock) {
    }

    Poll<Optional<Item>> poll() override {
        if (!tok_)
            tok_ = sock_->doAccept();
        return tok_->pollStream();
    }
private:
    AsyncServerSocket::Ptr sock_;
    io::intrusive_ptr<AsyncServerSocket::AcceptCompletionToken> tok_;
};

AcceptStream AsyncServerSocket::accept() {
    return AcceptStream(shared_from_this());
}

}
}

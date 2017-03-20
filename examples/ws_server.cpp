#include <futures/http/WsController.h>
#include <futures/io/Signal.h>
#include <futures/Timer.h>
#include "json.hpp"

using namespace futures;
using nlohmann::json;

class Broadcaster {
public:
  Broadcaster(EventExecutor *ev)
    : ev_(ev) {}

  void registerClient(websocket::Connection::Ptr conn) {
    conns_.insert(conn);
  }

  void unregisterClient(websocket::Connection::Ptr conn) {
    auto it = conns_.find(conn);
    if (it != conns_.end())
      conns_.erase(it);
  }

  void broadcast(const std::string &text) {
    FUTURES_DLOG(INFO) << "broadcast: " << conns_.size();
    for (auto it = conns_.begin(); it != conns_.end();) {
      if (!(*it)->good()) {
        it = conns_.erase(it);
        continue;
      }
      (*it)->sendText(text);
      ++it;
    }
  }

private:
  EventExecutor *ev_;
  std::set<websocket::Connection::Ptr> conns_;
};

class SocketIOHandler : public websocket::Handler {
public:
  enum {
    kSocketIOProtocolVersion = 4,
  };

  void onConnect(websocket::Connection::Ptr conn) {
    FUTURES_DLOG(INFO) << "CONNECTED";
    sendConnect(conn);
  }

  void onError(websocket::Connection::Ptr conn) {
  }

  void onClose(websocket::Connection::Ptr conn) {
  }

  BoxedFuture<Unit> onBinary(websocket::Connection::Ptr conn, const std::string &data) {
    FUTURES_LOG(INFO) << "binary: " << data;
    throw std::runtime_error("unsupported");
  }

  void emit(websocket::Connection::Ptr conn, const std::string& evname,
      const json& j) {
    json msg = json::array();
    msg[0] = evname;
    msg[1] = j;
    conn->sendText("42" + msg.dump());
  }

  void disconnect(websocket::Connection::Ptr conn) {
  }

  BoxedFuture<Unit> on(websocket::Connection::Ptr conn,
      const std::string &name, const json& j) {
    FUTURES_DLOG(INFO) << "Event: " << name << ", data: " << j.dump();
    return makeOk();
  }

  ~SocketIOHandler() {}

protected:
  BoxedFuture<Unit> onText(websocket::Connection::Ptr conn, const std::string &data) {
    FUTURES_DLOG(INFO) << "text: " << data;
    if (data.size() < 1) throw std::invalid_argument("invalid packet");
    switch (data[0]) {
    case '2':
      conn->sendText("3");
      return makeOk();
    case '4':
      return parseMessage(conn, data);
    default:
      throw std::invalid_argument("unknown packet type");
    }
  }

  BoxedFuture<Unit> parseMessage(websocket::Connection::Ptr conn, const std::string &data) {
    json j;
    switch (data[1]) {
    case '1':
      disconnect(conn);
      throw std::runtime_error("disconnected");
    // Event
    case '2':
      j = json::parse(data.c_str() + 2);
      if (!j.is_array() || j.size() < 1 || !j[0].is_string())
        throw std::invalid_argument("invalid event");
      if (j.size() > 1) {
        return on(conn, j[0], j[1]);
      } else if (j.size() == 1) {
        return on(conn, j[0], json());
      } else {
        throw std::invalid_argument("invalid event");
      }
    default:
      throw std::invalid_argument("packet type not supported");
    }
    return makeOk();
  }

  void sendConnect(websocket::Connection::Ptr conn) {
    conn->sendText("0{\"sid\":\"Us576lHxiLhevAZTAAAB\",\"upgrades\":[],\"pingInterval\":25000,\"pingTimeout\":60000}");
    conn->sendText("40");
  }

};

class EchoHandler final : public websocket::Handler {
public:
  EchoHandler(std::shared_ptr<Broadcaster> b)
    : b_(b) {
  }

  void onConnect(websocket::Connection::Ptr conn) {
    FUTURES_DLOG(INFO) << "CONNECTED";
    b_->registerClient(conn);
  }

  void onError(websocket::Connection::Ptr conn) {
    b_->unregisterClient(conn);
  }

  void onClose(websocket::Connection::Ptr conn) {
    b_->unregisterClient(conn);
  }

  BoxedFuture<Unit> onText(websocket::Connection::Ptr conn, const std::string &data) {
    FUTURES_LOG(INFO) << "text: " << data;
    if (data.empty())
      return makeOk();
    b_->broadcast(data);
    conn->sendText("ME: " + data);
    return makeOk();
  }

  BoxedFuture<Unit> onBinary(websocket::Connection::Ptr conn, const std::string &data) {
    FUTURES_LOG(INFO) << "binary: " << data;
    return makeOk();
  }

  ~EchoHandler() {}
private:
  std::shared_ptr<Broadcaster> b_;
};

int main(int argc, char *argv[])
{
  EventExecutor ev;
  folly::SocketAddress bind("0.0.0.0", 8044);
  auto ws = std::make_shared<websocket::WsServer>(&ev, bind);
  auto b = std::make_shared<Broadcaster>(&ev);

  ws->addRoute("^/echo/$", std::make_shared<EchoHandler>(b));
  ws->addRoute("^/socket.io/\\?(.*)$", std::make_shared<SocketIOHandler>());

  auto timer = makeLoop(0, [&ev, b] (int) {
      return delay(&ev, 1)
        | [b] (Unit) {
          FUTURES_DLOG(INFO) << "onTimer";
          b->broadcast("42{}");
          return makeContinue<Unit, int>(0);
        };
  });
  ev.spawn(std::move(timer));

  auto sig = io::signal(&ev, SIGINT)
    >> [b] (int num) {
      EventExecutor::current()->stop();
      return makeOk();
    };
  ev.spawn(std::move(sig));

  ws->start();
  ev.run();
  return 0;
}

#include <futures/EventExecutor.h>
#include <futures/Signal.h>
#include <futures/Timeout.h>
#include <futures/TcpStream.h>
#include <futures/http/HttpParser.h>
#include <futures/http/HttpController.h>
#include <futures/io/PipelinedRpcFuture.h>
#include <futures/io/IoStream.h>
#include <futures/io/AsyncSocket.h>
#include <futures/io/AsyncServerSocket.h>
#include <futures/dns/ResolverFuture.h>
#include <thread>
#include <iostream>
#include <futures/io/StreamAdapter.h>

#include "LRUCache11.hpp"

using namespace futures;

static void dumpBuf(const std::unique_ptr<folly::IOBuf> &buf) {
  folly::IOBufQueue q;
  q.append(buf->clone());
  IOBufStreambuf s(&q);
  std::istream is(&s);
  FUTURES_DLOG(INFO) << "================= " << buf->computeChainDataLength();
  FUTURES_DLOG(INFO) << is.rdbuf();
  FUTURES_DLOG(INFO) << "=================";
}

static std::string getField(const char *buf,
        const http_parser_url& url, int field) {
    if (url.field_set & (1 << field)) {
        return std::string(buf + url.field_data[field].off,
                url.field_data[field].len);
    } else {
        return "";
    }
}

struct UrlResult {
  std::string host;
  unsigned short port;
  std::string path;
};

static UrlResult parseUrl(const char *str, bool is_connect) {
    http_parser_url url;
    http_parser_url_init(&url);
    int ret = http_parser_parse_url(str, strlen(str), is_connect, &url);
    if (ret) throw std::runtime_error("failed to parse url");
    std::string host = getField(str, url, UF_HOST);
    bool is_https = getField(str, url, UF_SCHEMA) == "https";
    if (host.empty()) throw std::invalid_argument("no host");
    unsigned short port = url.port ? url.port : (is_https ? 443 : 80);

    std::string path;
    if (url.field_set & (1 << UF_PATH)) {
      path = str + url.field_data[UF_PATH].off;
    }
    if (path.empty()) path = "/";

    return UrlResult{host, port, path};
}

class HttpV1ProxyDecoder :
    public codec::DecoderBase<HttpV1ProxyDecoder, http::HttpFrame> {
public:
    using Out = http::HttpFrame;

    HttpV1ProxyDecoder()
      : impl_(new http::Parser(true)) {}

    Optional<Out> decode(folly::IOBufQueue &buf) {
      assert(!upgraded_);
      while (!buf.empty()) {
        auto front = buf.pop_front();
        size_t nparsed = impl_->execute(front.get());
        if (impl_->getParser().upgrade) {
          FUTURES_DLOG(INFO) << "Upgrade: " << nparsed
              << ", total: " << front->length();
          upgraded_ = true;
          if (impl_->getResult().method == HTTP_CONNECT) {
            FUTURES_DLOG(INFO) << "Upgrade to connect (raw tcp)";
            break;
          } else {
            throw IOError("upgrade unsupported");
          }
        } else if (nparsed != front->length()) {
          throw IOError("invalid http request");
        }
      }
      if (impl_->hasHeaderCompeleted() || upgraded_) {
          FUTURES_DLOG(INFO) << impl_->getResult().path;
          return Optional<Out>(impl_->moveResult());
      } else {
        return none;
      }
    }

private:
    std::unique_ptr<http::Parser> impl_;
    bool upgraded_ = false;
};

using DNSCache = lru11::Cache<std::string, std::vector<folly::IPAddress>>;

static DNSCache gDnsCache(1000, 100);

struct ConnState {
  bool connected_ = false;
  bool readed_ = false;
  bool is_connect = false;
  io::SocketChannel::Ptr inbound_;
  io::SocketChannel::Ptr outbound_;
  http::HttpFrame header_;
  http::HttpV1RequestEncoder enc_;
  UrlResult target_;
};

static BoxedFuture<folly::IPAddress> resolveWithCache(dns::AsyncResolver::Ptr resolver, const std::string &host)
{
  std::vector<folly::IPAddress> addrs;
  if (gDnsCache.tryGet(host, addrs)) {
    if (addrs.empty())
      gDnsCache.remove(host);
    else
      return makeOk(addrs[rand() % addrs.size()]);
  }
  return resolver->resolve(host, dns::AsyncResolver::EnableTypeA4)
    >> [host] (dns::ResolverResult result) {
      if (result.empty())
        throw std::runtime_error("failed to resolve");
      gDnsCache.insert(host, result);
      return makeOk(result[0]);
    };
}

static BoxedFuture<folly::Unit> forwardResponse(std::shared_ptr<ConnState> state, bool dir = false) {
  assert(state->readed_);
  auto in = state->outbound_;
  auto out = state->inbound_;
  if (dir) std::swap(in, out);
  return in->readStream()
    .andThen([state, out] (std::unique_ptr<folly::IOBuf> buf) {
        return out->write(std::move(buf));
    })
    .forEach([dir] (size_t size) {
        FUTURES_DLOG() << "Done send frame: " << size << ", dir: " << dir;
        return makeOk();
    })
    >> [dir] (Unit) {
      FUTURES_DLOG() << "Done forward response, dir: " << dir;
      // inbound ended
      if (!dir) {
        throw std::runtime_error("keep-alive unsupported");
      }
      return makeOk();
    };
}

// static BoxedFuture<folly::Unit> upgradeTcp(std::shared_ptr<ConnState> state) {
// }
class UpgradeException: public std::exception {
public:
  UpgradeException() {}
};

static BoxedFuture<folly::Unit> process(EventExecutor *ev,
    dns::AsyncResolver::Ptr resolver,
    TimerKeeper::Ptr conn_timer,
    io::SocketChannel::Ptr client) {
  auto state = std::make_shared<ConnState>();
  state->inbound_ = client;

  return io::FramedStream<HttpV1ProxyDecoder>(client)
    .andThen([resolver, conn_timer, state] (http::HttpFrame f) {
      FUTURES_DLOG(INFO) << f;
      if (!state->connected_ && !f.path.empty()) {
        state->is_connect = f.method == HTTP_CONNECT;
        state->target_ = parseUrl(f.path.c_str(), state->is_connect);
        state->header_ = std::move(f);

        return (resolveWithCache(resolver, state->target_.host)
          >> [state, conn_timer] (folly::IPAddress result) {
            folly::SocketAddress addr(result, state->target_.port);
            FUTURES_DLOG(INFO) << "connect: " << addr.getIPAddress().toJson();
            return timeout(conn_timer,
              io::SocketChannel::connect(EventExecutor::current(), addr), "connect timeout");
          }
          >> [state] (io::SocketChannel::Ptr out) {
            FUTURES_DLOG(INFO) << "connected";
            state->outbound_ = out;
            state->connected_ = true;
            // forward header (and partial body)
            folly::IOBufQueue q;
            if (state->is_connect) {
              static const char kConnResp[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
              q.append(folly::IOBuf::copyBuffer(kConnResp, sizeof(kConnResp) - 1));
              state->readed_ = true;
              return state->inbound_->write(q.move());
            } else {
              q.preallocate(2000, 4000);
              state->readed_ = state->header_.eof_;
              state->header_.path = state->target_.path;
              // we don't track connection response, use EOF
              state->header_.headers["Connection"] = "close";
              state->enc_.encode(http::Request(std::move(state->header_)), q);
              assert(!q.empty());
              return state->outbound_->write(q.move());
            }
          }
          >> [state] (ssize_t p) {
            FUTURES_DLOG(INFO) << "header written: " << p;
            if (state->is_connect) {
              // HTTP tunnel created
              return makeErr<Unit>(folly::make_exception_wrapper<UpgradeException>()).boxed();
            } else {
              if (state->readed_) {
                return forwardResponse(state);
              } else {
                return makeOk().boxed();
              }
            }
          }
          ).boxed();
      } else if (state->connected_) {
        state->readed_ = f.eof_;
        assert(!state->is_connect);
        return (state->outbound_->write(f.body.move())
          >> [state] (ssize_t p) {
            FUTURES_DLOG(INFO) << "written: " << p;
            if (state->readed_) {
              return forwardResponse(state);
            } else {
              return makeOk().boxed();
            }
          }).boxed();
      } else {
        FUTURES_DLOG(WARNING) << "drop frame";
        return makeOk().boxed();
      }
  })
  .forEach([client] (Unit) {
      FUTURES_DLOG(INFO) << "Done one frame";
      return makeOk();
  })
  .then([state] (Try<Unit> err) {
      if (err.hasException<UpgradeException>()) {
        FUTURES_DLOG(INFO) << "Upgrade";
        EventExecutor::current()->spawn(forwardResponse(state, true));
        return forwardResponse(state).boxed();
      }
      return ResultFuture<Unit>(std::move(err)).boxed();
  });
  // return ProxyFuture(ptr, client);
}

int main(int argc, char *argv[])
{
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " [host] [port]" << std::endl;
    return 1;
  }
  EventExecutor loop(true);
  folly::SocketAddress bindAddr(argv[1], atoi(argv[2]));
  auto s = std::make_shared<io::AsyncServerSocket>(&loop, bindAddr);

  auto resolver = std::make_shared<dns::AsyncResolver>(&loop);
  auto timer = std::make_shared<TimerKeeper>(&loop, 30.0);
  auto conn_timer = std::make_shared<TimerKeeper>(&loop, 5.0);

  auto f = s->accept()
    .forEach2([resolver, timer, conn_timer]
        (tcp::Socket client, folly::SocketAddress peer) {
        auto ev = EventExecutor::current();
        auto new_sock = std::make_shared<io::SocketChannel>(ev,
            std::move(client), peer);
        ev->spawn(
            timeout(timer, process(ev, resolver, conn_timer, new_sock))
              .error([] (folly::exception_wrapper err) {
                FUTURES_LOG(ERROR) << err.what();
              })
        );
    })
    << [] (Try<Unit> err) {
      if (err.hasException())
        FUTURES_LOG(ERROR) << err.exception().what();
      return makeOk();
    };
  auto sig = signal(&loop, SIGINT)
    >> [&] (int signum) {
        std::cerr << "killed by " << signum << std::endl;
        EventExecutor::current()->stop();
        return makeOk();
      };

  loop.spawn(std::move(f));
  loop.spawn(std::move(sig));
  loop.run();
  return 0;
}

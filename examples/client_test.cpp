#include <futures/EventExecutor.h>
#include <futures/io/Signal.h>
#include <futures/Timeout.h>
#include <futures/http/HttpClient.h>
#include <futures/core/Compression.h>

using namespace futures;

static BoxedFuture<folly::Unit> fetch(EventExecutor *ev, io::SSLContext *ctx, const char *s) {
    auto resolver = std::make_shared<dns::AsyncResolver>(ev);
    http::Url url = http::HttpClient::parseUrl(s);
    auto client = std::make_shared<http::HttpClient>(ev, ctx, resolver, url);
    FUTURES_DLOG(INFO) << "path: " << url.path;
    return client->get(url.path)
        >> [client] (http::Response req) {
            std::cerr << req << std::endl;
            std::string body;
            auto it = req.headers.find("Content-Encoding");
            if (it == req.headers.end()) {
                req.body.appendToString(body);
            } else if (it->second == "gzip") {
                auto codec = folly::io::getCodec(folly::io::CodecType::GZIP);
                FUTURES_LOG(INFO) << "size: " << req.body.front()->length();
                auto out = codec->uncompress(req.body.front());
                FUTURES_LOG(INFO) << "uncompressed size: " << out->computeChainDataLength();
                auto r = out->coalesce();
                body = r.toString();
            } else {
                body = "<UNSUPPORTED Content-Encoding>";
            }
            std::cerr << "========" << std::endl;
            std::cout << body << std::endl;
            std::cerr << "========" << std::endl;

            return client->close();
        };
#if 0
        >> [ev] (Unit) {
            FUTURES_DLOG(INFO) << "delay";
            return delay(ev, 20.0);
        }
        >> [client] (Unit) {
            return client->get("/");
        }
        >> [client] (http::Response r) {
            std::cerr << r << std::endl;
            return client->close();
        };
#endif
}

int main(int argc, char *argv[])
{

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " url" << std::endl;
        return 1;
    }

    EventExecutor loop(true);
    io::SSLContext ctx;

    auto f = fetch(&loop, &ctx, argv[1])
        << [] (Try<Unit> err) {
            if (err.hasException())
                FUTURES_LOG(ERROR) << err.exception().what();
            FUTURES_DLOG(INFO ) << "task: " << EventExecutor::current()->getRunning();
            EventExecutor::current()->stop();
            return makeOk();
        };
    auto sig = io::signal(&loop, SIGINT)
        .andThen([&] (int signum) {
        FUTURES_DLOG(INFO) << "killed by " << signum;
        EventExecutor::current()->stop();
        return makeOk();
    });
    loop.spawn(std::move(sig));

    loop.spawn(std::move(f));
    loop.run();
    return 0;
}


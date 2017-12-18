#include <futures_zookeeper/ZkClient.h>
#include <futures/EventExecutor.h>
#include <futures/io/Signal.h>
#include <futures/io/PipeChannel.h>
#include <futures/io/IoFuture.h>
#include <futures/codec/LineBasedDecoder.h>
#include <futures_readline/Console.h>

using namespace futures;

std::vector<std::string> split(const std::string& str, const std::string& delim)
{
    std::vector<std::string> tokens;
    size_t prev = 0, pos = 0;
    do
    {
        pos = str.find(delim, prev);
        if (pos == std::string::npos) pos = str.length();
        auto token = str.substr(prev, pos-prev);
        if (!token.empty()) tokens.push_back(token);
        prev = pos + delim.length();
    }
    while (pos < str.length() && prev < str.length());
    return tokens;
}


class ZkCliConsole final : public readline::Console {
public:
    ZkCliConsole(EventExecutor *ev, zookeeper::ZkClient::Ptr c)
        : readline::Console(ev, "zkcli:0> "), client(c) {}

    BoxedFuture<Unit> onCommand(const std::string &cmd) override {
        return runCommand(cmd)
            .error([] (folly::exception_wrapper w) {
                FUTURES_LOG(ERROR) << w.what();
            });
    }

    BoxedFuture<Unit> runCommand(const std::string& cmd) {
        if (cmd.empty()) return makeOk();
        addPrompt();

        FUTURES_DLOG(INFO) << "Run: " << cmd;
        auto v = split(cmd, " ");
        if (v.size() < 1) {
            FUTURES_LOG(ERROR) << "bad command";
            return makeOk();
        }
        if (v[0] == "ls" && v.size() > 1) {
            // bool watch = v.size() > 2 && v[2] == "watch";
            return client->getChildren(v[1])
                | [] (zookeeper::StringList ls) {
                    for (auto &e: ls)
                        std::cerr << e << std::endl;
                    return unit;
                };
        } else if (v[0] == "create" && v.size() > 2) {
            bool emp = v.size() > 3 && v[3] == "+e";
            int flags = 0;
            if (emp) flags |= zookeeper::ZkClient::kEphemeral;
            return client->createNode(v[1], v[2], nullptr, flags)
                | [] (std::string name) {
                    FUTURES_LOG(INFO) << "Created: " << name;
                    return unit;
                };
        } else if (v[0] == "delete" && v.size() > 1) {
            return client->deleteNode(v[1], 0)
                | [] (Unit) {
                    FUTURES_LOG(INFO) << "Deleted";
                    return unit;
                };
        } else if (v[0] == "get" && v.size() > 1) {
            return client->getData(v[1])
                | [] (std::string v) {
                    FUTURES_LOG(INFO) << "data: " << v;
                    return unit;
                };
        } else if (v[0] == "exists" && v.size() > 1) {
            return client->existsNode(v[1])
                | [] (zookeeper::NodeState v) {
                    FUTURES_LOG(INFO) << v;
                    return unit;
                };
        } else if (v[0] == "verbose") {
            zookeeper::ZkClient::setLogLevel(zookeeper::ZkClient::LogLevel::Debug);
            FUTURES_LOG(INFO) << "verbose";
            return makeOk();
        } else {
            FUTURES_LOG(ERROR) << "unknown command";
            return makeOk();
        }
    }

    BoxedFuture<Unit> onEof() override {
        FUTURES_LOG(INFO) << "exiting...";
        EventExecutor::current()->stop();
        return makeOk();
    }

    void onError(folly::exception_wrapper w) override {
        if (!w.is_compatible_with<FutureCancelledException>())
            FUTURES_LOG(FATAL) << "unexpected error: " << w.what();
    }

private:
    int count = 0;
    zookeeper::ZkClient::Ptr client;

    void addPrompt() {
        count++;
        setPrompt("zkcli:" + std::to_string(count) + "> ");
    }
};

int main(int argc, char *argv[])
{
    EventExecutor ev(true);
    ::fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);

    auto zk = std::make_shared<zookeeper::ZkClient>(&ev, argv[1]);

    auto watcher = zk->eventStream()
        .forEach([] (zookeeper::WatchedEvent ev) {
            FUTURES_LOG(INFO) << "New event: " << ev.type << ", state = "
                << ev.state << ", path = " << ev.path;
        })
        .error([] (folly::exception_wrapper w) {
            FUTURES_LOG(ERROR) << "watcher ended: " << w.what();
        });
    ev.spawn(std::move(watcher));

    auto console = std::make_shared<ZkCliConsole>(&ev, zk);

    auto sig = io::signal(&ev, SIGINT)
        | [] (int num) {
            FUTURES_LOG(INFO) << "killed by " << num;
            EventExecutor::current()->stop();
            return unit;
        };

    ev.spawn(std::move(sig));
    console->start();
    ev.run();

    return 0;
}

#include <futures_zookeeper/ZkClient.h>
#include <futures/EventExecutor.h>
#include <futures/io/Signal.h>
#include <futures/io/PipeChannel.h>
#include <futures/io/IoFuture.h>
#include <futures/codec/LineBasedDecoder.h>
#include <futures/Promise.h>
#include <futures/Timer.h>
#include <futures/Timeout.h>
#include <futures_readline/Console.h>

using namespace futures;

extern const int ZOO_CONNECTED_STATE;

class DistributedLock : public std::enable_shared_from_this<DistributedLock> {
public:
    DistributedLock(zookeeper::ZkClient::Ptr client, const std::string &pathname)
        : client_(client), pathname_(pathname) {
    }

    BoxedFuture<Unit> lock() {
        auto self = shared_from_this();
        return getClient()->createNode(pathname_, std::string(), nullptr, 0)
            << [self, this] (Try<std::string> parent) {
                try {
                    parent.throwIfFailed();
                } catch (zookeeper::NodeExistsException &ex) {
                    // ignore
                }
                return acquireLock();
            };
    }

    BoxedFuture<Unit> unlock() {
        if (nodename_.empty())
            throw std::logic_error("not locked");
        return getClient()->deleteNode(pathname_ + "/" + nodename_, false);
    }

    zookeeper::ZkClient::Ptr getClient() { return client_; }

private:
    zookeeper::ZkClient::Ptr client_;
    std::string pathname_;
    std::string nodename_;

    std::string filename(const std::string &s) {
        auto p = s.find_last_of('/');
        return s.substr(p+1);
    }

    BoxedFuture<Unit> acquireLock() {
        auto self = shared_from_this();
        return getClient()->createNode(pathname_ + "/lock-", std::string(), nullptr,
                zookeeper::ZkClient::kSequence | zookeeper::ZkClient::kEphemeral)
            >> [self, this] (std::string lockname) {
                nodename_ = filename(lockname);
                FUTURES_DLOG(INFO) << "lock: " << lockname;
                return makeLoop(0, [self, this] (int n) {
                    return getClient()->getChildren(pathname_)
                        >> [self, this, n] (zookeeper::GetChildrenResult result) {
                            std::sort(result.begin(), result.end());
                            FUTURES_DLOG(INFO) << "result: " << result[0] << ", " << nodename_;
                            if (nodename_ == result[0]) {
                                return makeOk(makeBreak<Unit, int>(unit)).boxed();
                            } else {
                                auto cur = result[0];
                                // find previous node
                                for (auto &e: result) {
                                    if (e < nodename_) cur = e;
                                    else break;
                                }
                                cur = pathname_ + "/" + cur;
                                return (getClient()->existsNodeW(cur)
                                    << [self, this, cur] (Try<zookeeper::ExistsWResult> state) {
                                        if (state.hasException()) {
                                            return makeOk(makeContinue<Unit, int>(0)).boxed();
                                        } else {
                                            FUTURES_DLOG(INFO) << "waiting for " << cur;
                                            return (std::get<1>(*state)
                                                >> [] (zookeeper::WatchedEvent ev) {
                                                    return makeOk(makeContinue<Unit, int>(0));
                                                }).boxed();
                                        }
                                    }).boxed();
                            }
                    };
            });
        };
    }
};

int main(int argc, char *argv[])
{
    EventExecutor ev(true);
    auto zk = std::make_shared<zookeeper::ZkClient>(&ev, argv[1]);
    // auto monitor = std::make_shared<WatcherMonitor>(zk);
    auto lock = std::make_shared<DistributedLock>(zk, "/test/lock1");
    auto f = timeout(&ev, zk->waitConnect(), 10.0)
        >> [lock] (Unit) {
            return lock->lock();
        }
        >> [&ev] (Unit) {
            FUTURES_LOG(INFO) << "locked";
            return delay(&ev, 3);
        }
        >> [&ev, lock] (Unit) {
            return lock->unlock();
        }
        >> [] (Unit) {
            FUTURES_LOG(INFO) << "unlocked";
            EventExecutor::current()->stop();
            return makeOk();
        };
    auto sig = io::signal(&ev, SIGINT)
        | [] (int num) {
            FUTURES_LOG(INFO) << "exit";
            EventExecutor::current()->stop();
            return unit;
        };
    // monitor->start();
    ev.spawn(std::move(f));
    ev.spawn(std::move(sig));
    ev.run();
    return 0;
}

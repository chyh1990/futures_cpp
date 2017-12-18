#pragma once

#include <deque>
#include <map>
#include <futures_zookeeper/Exception.h>
#include <futures/EventExecutor.h>
#include <futures/io/WaitHandleBase.h>
#include <futures/Promise.h>
#include <futures_zookeeper/Request.h>

typedef struct _zhandle zhandle_t;
struct String_vector;
struct ACL_vector;

namespace futures {
namespace zookeeper {

folly::exception_wrapper makeZkException(int code);

class CommandToken : public io::CompletionToken {
public:
    enum Type {
        GET_CHILDREN,
        GET_CHILDREN2,
        GET,
        SET,
        GET_ACL,
        SET_ACL,
        CREATE,
        DELETE,
        EXISTS,
        SYNC,
    };

    CommandToken(Type type)
        : io::CompletionToken(io::IOObject::OpWrite), type_(type) {
        memset(&state_, 0, sizeof(state_));
    }

    void onCancel(CancelReason r) override {
        // cannot cancel
    }

    void setError(int rc) {
        assert(rc != 0);
        rc_ = rc;
        notifyDone();
    }

    void setWatch(PromiseFuture<WatchedEvent> &&w) {
        watch_ = std::move(w);
    }

    PromiseFuture<WatchedEvent> getWatch() {
        return std::move(watch_).value();
    }

    int getError() const { return rc_; }

    std::vector<std::string>& getStringList() {
        return strings_;
    }

    NodeState& getStat() {
        return state_;
    }

    std::string &getData() {
        return data_;
    }

private:
    Type type_;

    int rc_ = 0;
    std::string data_;
    std::vector<std::string> strings_;
    NodeState state_;
    Optional<PromiseFuture<WatchedEvent>> watch_;
};

class ConnectToken : public io::CompletionToken {
public:
    ConnectToken()
        : io::CompletionToken(io::IOObject::OpConnect) {}
    void setError(int rc) {
        assert(rc != 0);
        rc_ = rc;
        notifyDone();
    }

    int getError() const { return rc_; }

    void onCancel(CancelReason r) override {}

    ~ConnectToken() {
        cleanup(CancelReason::UserCancel);
    }
private:
    int rc_ = 0;
};

class EventStreamToken : public io::CompletionToken {
public:
    EventStreamToken()
        : io::CompletionToken(io::IOObject::OpRead) {}

    void setError(int rc) {
        assert(rc != 0);
        rc_ = rc;
        notifyDone();
    }

    int getError() const { return rc_; }

    void onCancel(CancelReason r) override {}

    void pushEvent(const WatchedEvent& ev) {
        events_.push_back(ev);
        notify();
    }

    std::deque<WatchedEvent> &events() {
        return events_;
    }

private:
    int rc_ = 0;
    std::deque<WatchedEvent> events_;
};

class ConnectFuture;
class GetChildrenCommandFuture;
class GetChildrenWCommandFuture;
class GetChildren2CommandFuture;
class GetChildren2WCommandFuture;
class GetCommandFuture;
class GetWCommandFuture;
class SetCommandFuture;
class CreateCommandFuture;
class DeleteCommandFuture;
class ExistsCommandFuture;
class ExistsWCommandFuture;
class SyncCommandFuture;

class ZkEventStream;

class ZkClient : public io::IOObject,
    public std::enable_shared_from_this<ZkClient> {
public:
    using Ptr = std::shared_ptr<ZkClient>;

    enum {
        kMaxDataSize = 1 * 1024 * 1024,
    };

    enum class LogLevel : int {
        Error = 1,
        Warn = 2,
        Info = 3,
        Debug = 4,
    };

    enum CreateFlags {
        kEphemeral = 0x01,
        kSequence = 0x02,
    };

    static void setLogLevel(LogLevel level);

    ZkClient(EventExecutor *ev, const std::string &hosts);
    ~ZkClient();

    ZkClient(const ZkClient&) = delete;
    ZkClient& operator=(const ZkClient&) = delete;

    io::intrusive_ptr<ConnectToken> doConnect();

    io::intrusive_ptr<CommandToken> doGetChildren(const std::string &path, bool watch);
    io::intrusive_ptr<CommandToken> doGetChildren2(const std::string &path, bool watch);
    io::intrusive_ptr<CommandToken> doGet(const std::string &path, bool watch);
    io::intrusive_ptr<CommandToken> doSet(const std::string &path,
            const char *buffer, int len, int version);

    io::intrusive_ptr<CommandToken> doCreate(const std::string &path,
            const char *value, int valuelen,
            const struct ACL_vector *acl, int flags);
    io::intrusive_ptr<CommandToken> doDelete(const std::string &path,
            int version);
    io::intrusive_ptr<CommandToken> doExists(const std::string &path,
            bool watch);
    io::intrusive_ptr<CommandToken> doSync(const std::string &path);
    io::intrusive_ptr<EventStreamToken> doEventStream();

    // future API
    ConnectFuture waitConnect();
    GetChildrenCommandFuture getChildren(const std::string &path);
    GetChildrenWCommandFuture getChildrenW(const std::string &path);
    GetChildren2CommandFuture getChildren2(const std::string &path);
    GetChildren2WCommandFuture getChildren2W(const std::string &path);
    GetCommandFuture getData(const std::string &path);
    GetWCommandFuture getDataW(const std::string &path);

    SetCommandFuture setData(const std::string &path, const std::string &data, int version);
    CreateCommandFuture createNode(const std::string &path, const std::string &data,
            const struct ACL_vector *acl, int flags);
    DeleteCommandFuture deleteNode(const std::string &path, int version);

    ExistsCommandFuture existsNode(const std::string &path);
    ExistsWCommandFuture existsNodeW(const std::string &path);

    SyncCommandFuture syncNode(const std::string &path);

    ZkEventStream eventStream();
private:
    zhandle_t* zh_{nullptr};
    ev::io io_;
    ev::timer timer_;
    std::multimap<std::string, Promise<WatchedEvent>> watchers_;

    static void defaultWatcherHandler(zhandle_t *zh, int type,
            int state, const char *path, void *ctx);

    void updateWatcher();
    void addWatch(CommandToken *tok, const std::string &path);
    void updateCommand(int rc, CommandToken *tok);

    void onEvent(ev::io &watcher, int revents);
    void onTimer(ev::timer &watcher, int revents);
    void processRequest(int revents);

    static void onVoidCompletion(int rc, const void *data);
    static void onStatCompletion(int rc, const struct Stat *stat, const void *data);
    static void onDataCompletion(int rc, const char *value, int len, const struct Stat *stat, const void *data);

    static void onStringsCompletion(int rc, const struct String_vector *, const void *data);
    static void onStringsStatCompletion(int rc, const struct String_vector *,
            const struct Stat *stat, const void *data);
    static void onStringCompletion(int rc, const char *, const void *data);
};


}
}

#include <futures_zookeeper/ZkFuture-inl.h>

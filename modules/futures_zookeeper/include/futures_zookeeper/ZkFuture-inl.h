#pragma once

#include <futures/Future.h>
#include <futures/Stream.h>
#include <futures_zookeeper/ZkClient.h>

namespace futures {
namespace zookeeper {

class ConnectFuture : public FutureBase<ConnectFuture, Unit> {
public:
    using Item = Unit;

    ConnectFuture(ZkClient::Ptr ctx)
        : ctx_(ctx) {}

    Poll<Item> poll() override {
        if (!tok_)
            tok_ = ctx_->doConnect();
        switch (tok_->getState()) {
        case ConnectToken::STARTED:
            tok_->park();
            return Poll<Item>(not_ready);
        case ConnectToken::DONE:
            if (tok_->getError()) {
                return Poll<Item>(makeZkException(tok_->getError()));
            } else {
                return makePollReady(unit);
            }
        case CommandToken::CANCELLED:
            return Poll<Item>(FutureCancelledException());
        }
    }

private:
    ZkClient::Ptr ctx_;
    io::intrusive_ptr<ConnectToken> tok_;
};

template <typename Derived, typename T>
class GenericCommandFuture : public FutureBase<Derived, T>
{
public:
    using Item = T;

    GenericCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : ctx_(ctx), path_(path) {}


    Poll<Item> poll() override {
        if (!tok_) {
            static_cast<Derived*>(this)->startCommand();
        }
        switch (tok_->getState()) {
        case CommandToken::STARTED:
            tok_->park();
            return Poll<Item>(not_ready);
        case CommandToken::DONE:
            if (tok_->getError()) {
                return Poll<Item>(makeZkException(tok_->getError()));
            } else {
                return static_cast<Derived*>(this)->pollReal();
            }
        case CommandToken::CANCELLED:
            return Poll<Item>(FutureCancelledException());
        }
    }

    Poll<Item> pollReal() { assert(0 && "base class"); }
    void startCommand() { assert(0 && "base class"); }
protected:
    ZkClient::Ptr ctx_;
    io::intrusive_ptr<CommandToken> tok_;
    std::string path_;
};

class GetChildrenCommandFuture
    : public GenericCommandFuture<GetChildrenCommandFuture, StringList> {
public:
    using Item = StringList;

    GetChildrenCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path) {}

    void startCommand() {
        tok_ = ctx_->doGetChildren(std::move(path_), false);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(std::move(tok_->getStringList()));
    }
};

using GetChildrenWResult = std::tuple<StringList, PromiseFuture<WatchedEvent>>;
class GetChildrenWCommandFuture
    : public GenericCommandFuture<GetChildrenWCommandFuture, GetChildrenWResult> {
public:
    using Item = GetChildrenWResult;

    GetChildrenWCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path) {}

    void startCommand() {
        tok_ = ctx_->doGetChildren(std::move(path_), true);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(std::make_tuple(std::move(tok_->getStringList()),
                    tok_->getWatch()));
    }
};

class GetChildren2CommandFuture
    : public GenericCommandFuture<GetChildren2CommandFuture, GetChildren2Result> {
public:
    using Item = GetChildren2Result;

    GetChildren2CommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path) {}

    void startCommand() {
        tok_ = ctx_->doGetChildren2(std::move(path_), false);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(
                std::make_tuple(std::move(tok_->getStringList()),
                tok_->getStat()));
    }
};

using GetChildren2WResult = std::tuple<StringList, NodeState, PromiseFuture<WatchedEvent>>;
class GetChildren2WCommandFuture
    : public GenericCommandFuture<GetChildren2WCommandFuture, GetChildren2WResult> {
public:
    using Item = GetChildren2WResult;

    GetChildren2WCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path) {}

    void startCommand() {
        tok_ = ctx_->doGetChildren2(std::move(path_), true);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(
                std::make_tuple(std::move(tok_->getStringList()),
                tok_->getStat(), tok_->getWatch()));
    }
};

class GetCommandFuture
    : public GenericCommandFuture<GetCommandFuture, GetResult> {
public:
    using Item = GetResult;

    GetCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path) {}

    void startCommand() {
        tok_ = ctx_->doGet(std::move(path_), false);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(std::move(tok_->getData()));
    }
private:
};

using GetWResult = std::tuple<GetResult, PromiseFuture<WatchedEvent>>;
class GetWCommandFuture
    : public GenericCommandFuture<GetWCommandFuture, GetWResult> {
public:
    using Item = GetWResult;

    GetWCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path) {}

    void startCommand() {
        tok_ = ctx_->doGet(std::move(path_), true);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(std::make_tuple(std::move(tok_->getData()), tok_->getWatch()));
    }
private:
};


class SetCommandFuture
    : public GenericCommandFuture<SetCommandFuture, NodeState> {
public:
    using Item = NodeState;

    SetCommandFuture(ZkClient::Ptr ctx, const std::string &path,
            const std::string &data, int version)
        : GenericCommandFuture(ctx, path), version_(version), data_(data) {}

    void startCommand() {
        auto p = data_.empty() ? "" : data_.data();
        tok_ = ctx_->doSet(std::move(path_), p, data_.size(), version_);
        data_.clear();
    }

    Poll<Item> pollReal() {
        return Poll<Item>(tok_->getStat());
    }
private:
    int version_;
    std::string data_;
};

class CreateCommandFuture
    : public GenericCommandFuture<CreateCommandFuture, std::string>
{
public:
    using Item = std::string;
    CreateCommandFuture(ZkClient::Ptr ctx, const std::string &path,
            const std::string &data, const struct ACL_vector *acl, int flags)
        : GenericCommandFuture(ctx, path), data_(data), flags_(flags)
    {
    }

    void startCommand() {
        auto p = data_.empty() ? "" : data_.data();
        tok_ = ctx_->doCreate(std::move(path_), p, data_.size(),
                nullptr, flags_);
        data_.clear();
    }

    Poll<Item> pollReal() {
        return Poll<Item>(tok_->getData());
    }

private:
    std::string data_;
    int flags_;
};

class DeleteCommandFuture
    : public GenericCommandFuture<DeleteCommandFuture, Unit>
{
public:
    using Item = Unit;

    DeleteCommandFuture(ZkClient::Ptr ctx, const std::string &path, int version)
        : GenericCommandFuture(ctx, path), version_(version)
    {
    }

    void startCommand() {
        tok_ = ctx_->doDelete(std::move(path_), version_);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(unit);
    }
private:
    int version_;
};

class ExistsCommandFuture
    : public GenericCommandFuture<ExistsCommandFuture, NodeState>
{
public:
    using Item = NodeState;

    ExistsCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path)
    {
    }

    void startCommand() {
        tok_ = ctx_->doExists(std::move(path_), false);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(tok_->getStat());
    }
private:
};

using ExistsWResult = std::tuple<NodeState, PromiseFuture<WatchedEvent>>;
class ExistsWCommandFuture
    : public GenericCommandFuture<ExistsWCommandFuture, ExistsWResult>
{
public:
    using Item = ExistsWResult;

    ExistsWCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path)
    {
    }

    void startCommand() {
        tok_ = ctx_->doExists(std::move(path_), true);
    }

    Poll<Item> pollReal() {
        return Poll<Item>(std::make_tuple(tok_->getStat(), tok_->getWatch()));
    }
private:
};


class SyncCommandFuture
    : public GenericCommandFuture<SyncCommandFuture, Unit>
{
public:
    using Item = Unit;

    SyncCommandFuture(ZkClient::Ptr ctx, const std::string &path)
        : GenericCommandFuture(ctx, path)
    {
    }

    void startCommand() {
        tok_ = ctx_->doSync(std::move(path_));
    }

    Poll<Item> pollReal() {
        return Poll<Item>(unit);
    }
private:
};



class ZkEventStream : public StreamBase<ZkEventStream, WatchedEvent>
{
public:
    using Item = WatchedEvent;

    ZkEventStream(ZkClient::Ptr ctx)
        : ctx_(ctx) {}

    Poll<Optional<Item>> poll() override {
        if (!tok_)
            tok_ = ctx_->doEventStream();
        if (!tok_->events().empty()) {
            auto v = std::move(tok_->events().front());
            tok_->events().pop_front();
            return makePollReady(Optional<Item>(std::move(v)));
        }
        switch (tok_->getState()) {
        case EventStreamToken::STARTED:
            tok_->park();
            return Poll<Optional<Item>>(not_ready);
        case EventStreamToken::DONE:
            if (tok_->getError()) {
                return Poll<Optional<Item>>(makeZkException(tok_->getError()));
            } else {
                return makePollReady(Optional<Item>());
            }
        case EventStreamToken::CANCELLED:
            return Poll<Optional<Item>>(FutureCancelledException());
        };
    }
private:
    ZkClient::Ptr ctx_;
    io::intrusive_ptr<EventStreamToken> tok_;
};

}
}

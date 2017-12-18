#include <zookeeper.h>
#include <zookeeper_log.h>
#include <futures_zookeeper/ZkClient.h>

namespace futures {
namespace zookeeper {

folly::exception_wrapper makeZkException(int code) {
    if (code > ZAPIERROR) {
      return folly::make_exception_wrapper<SystemErrorException>(code);
    } else {
      switch (code) {
        case ZNONODE:
          return folly::make_exception_wrapper<NoNodeException>();
        case ZNOAUTH:
          return folly::make_exception_wrapper<NoAuthException>();
        case ZBADVERSION:
          return folly::make_exception_wrapper<BadVersionException>();
        case ZNOCHILDRENFOREPHEMERALS:
          return folly::make_exception_wrapper<NoChildrenForEphemeralsException>();
        case ZNODEEXISTS:
          return folly::make_exception_wrapper<NodeExistsException>();
        case ZNOTEMPTY:
          return folly::make_exception_wrapper<NotEmptyException>();
        default:
          return folly::make_exception_wrapper<ApiErrorException>(code);
      }
    }
}

static const char* state2String(int state){
  if (state == 0)
    return "CLOSED_STATE";
  if (state == ZOO_CONNECTING_STATE)
    return "CONNECTING_STATE";
  if (state == ZOO_ASSOCIATING_STATE)
    return "ASSOCIATING_STATE";
  if (state == ZOO_CONNECTED_STATE)
    return "CONNECTED_STATE";
  if (state == ZOO_EXPIRED_SESSION_STATE)
    return "EXPIRED_SESSION_STATE";
  if (state == ZOO_AUTH_FAILED_STATE)
    return "AUTH_FAILED_STATE";

  return "INVALID_STATE";
}

static const char* type2String(int state){
  if (state == ZOO_CREATED_EVENT)
    return "CREATED_EVENT";
  if (state == ZOO_DELETED_EVENT)
    return "DELETED_EVENT";
  if (state == ZOO_CHANGED_EVENT)
    return "CHANGED_EVENT";
  if (state == ZOO_CHILD_EVENT)
    return "CHILD_EVENT";
  if (state == ZOO_SESSION_EVENT)
    return "SESSION_EVENT";
  if (state == ZOO_NOTWATCHING_EVENT)
    return "NOTWATCHING_EVENT";

  return "UNKNOWN_EVENT_TYPE";
}

ZkClient::ZkClient(EventExecutor *ev, const std::string &hosts)
    : io::IOObject(ev), io_(ev->getLoop()), timer_(ev->getLoop()) {
    zh_ = zookeeper_init(hosts.c_str(), ZkClient::defaultWatcherHandler,
            30000, nullptr, this, 0);
    if (!zh_) throw IOError(std::error_code(errno, std::system_category()));

    FUTURES_DLOG(INFO) << "inited";
    io_.set<ZkClient, &ZkClient::onEvent>(this);
    timer_.set<ZkClient, &ZkClient::onTimer>(this);
    // setLogLevel(LogLevel::Debug);
    updateWatcher();
}

inline EventType toET(int t) {
  if (t == ZOO_CREATED_EVENT)
    return EventType::Created;
  else if (t == ZOO_DELETED_EVENT)
    return EventType::Deleted;
  else if (t == ZOO_CHANGED_EVENT)
    return EventType::Changed;
  else if (t == ZOO_CHILD_EVENT)
    return EventType::Child;
  else if (t == ZOO_SESSION_EVENT)
    return EventType::Session;
  else if (t == ZOO_NOTWATCHING_EVENT)
    return EventType::NotWatching;
  else
    return EventType::Unknown;
}

void ZkClient::defaultWatcherHandler(zhandle_t *zh, int type,
    int state, const char *path, void *ctx)
{
    FUTURES_DLOG(INFO) << "Watcher " << type2String(type) << ", state = " << state2String(state)
      << ", path = " << path;
    ZkClient *self = static_cast<ZkClient*>(ctx);
    if (type == ZOO_SESSION_EVENT) {
      if (state == ZOO_EXPIRED_SESSION_STATE) {
        FUTURES_LOG(ERROR) << "zookeeper session expired";
        self->watchers_.clear();
      } else if (state == ZOO_CONNECTED_STATE) {
        auto &c = self->getPending(io::IOObject::OpConnect);
        while (!c.empty()) {
          auto p = static_cast<ConnectToken*>(&c.front());
          p->notifyDone();
        }
      }
    }

    if (path) {
      auto it = self->watchers_.find(path);
      for (; it != self->watchers_.end(); ) {
        if (it->first != path)
          break;
        it->second.setValue(WatchedEvent{toET(type), state, path});
        it = self->watchers_.erase(it);
      }
    }

    auto &reads = self->getPending(io::IOObject::OpRead);
    if (reads.empty()) return;
    auto ev = static_cast<EventStreamToken*>(&reads.front());
    ev->pushEvent(WatchedEvent{toET(type), state, path ? std::string(path) : std::string()});
}

void ZkClient::setLogLevel(LogLevel level) {
    zoo_set_debug_level(static_cast<ZooLogLevel>(level));
}

void ZkClient::updateWatcher() {
    int fd = -1;
    int interest = 0;
    struct timeval tv;
    int r = zookeeper_interest(zh_, &fd, &interest, &tv);
    if (r) throw ZookeeperException("zookeeper_interest", r);
    if (fd != -1) {
        int ev = 0;
        if (interest & ZOOKEEPER_READ)
            ev |= ev::READ;
        if (interest & ZOOKEEPER_WRITE)
            ev |= ev::WRITE;
        io_.set(fd, ev);
        io_.start();
    } else {
        io_.stop();
    }
    double ts = tv.tv_sec + tv.tv_usec * 1e-6;
    if (ts >= 0) {
        FUTURES_DLOG(INFO) << "TIMER: " << ts;
        timer_.start(ts);
    }
}

ZkClient::~ZkClient() {
    if (zh_) {
      FUTURES_DLOG(INFO) << "closing";
      zookeeper_close(zh_);
    }
}

void ZkClient::onEvent(ev::io &watcher, int revents) {
    processRequest(revents);
}

void ZkClient::onTimer(ev::timer &watcher, int revents) {
    if (revents & ev::TIMER) {
        processRequest(revents);
    }
}

void ZkClient::onVoidCompletion(int rc, const void *data)
{
  CommandToken *tok = (CommandToken*)(data);
  if (rc) {
    tok->setError(rc);
  } else {
    tok->notifyDone();
  }
  tok->decRef();
}

inline void setState(const struct Stat*stat, NodeState *s) {
#define __sv(f) s->f = stat->f;
      __sv(czxid);
      __sv(mzxid);
      __sv(ctime);
      __sv(mtime);
      __sv(version);
      __sv(cversion);
      __sv(aversion);
      __sv(ephemeralOwner);
      __sv(dataLength);
      __sv(numChildren);
      __sv(pzxid);
#undef __sv
}

void ZkClient::onDataCompletion(int rc, const char *value, int len,
    const struct Stat *stat, const void *data)
{
  CommandToken *tok = (CommandToken*)(data);
  if (rc) {
    tok->setError(rc);
  } else {
    if (stat) setState(stat, &tok->getStat());
    if (value) tok->getData().assign(value, len);
    tok->notifyDone();
  }
  tok->decRef();
}

void ZkClient::onStatCompletion(int rc, const struct Stat *stat, const void *data)
{
  CommandToken *tok = (CommandToken*)(data);
  if (rc) {
    tok->setError(rc);
  } else {
    if (stat)
      setState(stat, &tok->getStat());
    tok->notifyDone();
  }
  tok->decRef();
}

void ZkClient::onStringsCompletion(int rc, const struct String_vector *v, const void *data)
{
  CommandToken *tok = (CommandToken*)(data);
  if (rc) {
    tok->setError(rc);
  } else {
    auto &l = tok->getStringList();
    if (v) {
      for (int i = 0; i < v->count; ++i)
        l.push_back(v->data[i]);
    }
    tok->notifyDone();
  }
  tok->decRef();
}

void ZkClient::onStringsStatCompletion(int rc, const struct String_vector *v,
    const struct Stat *stat, const void *data)
{
  CommandToken *tok = (CommandToken*)(data);
  if (rc) {
    tok->setError(rc);
  } else {
    auto &l = tok->getStringList();
    if (stat) setState(stat, &tok->getStat());
    if (v) {
      for (int i = 0; i < v->count; ++i)
        l.push_back(v->data[i]);
    }
    tok->notifyDone();
  }
  tok->decRef();
}

void ZkClient::onStringCompletion(int rc, const char *value, const void *data)
{
  CommandToken *tok = (CommandToken*)(data);
  if (rc) {
    tok->setError(rc);
  } else {
    if (value) tok->getData().assign(value);
    tok->notifyDone();
  }
  tok->decRef();
}

static void checkPath(const std::string &path) {
  // if (path.empty() || path[0] != '/')
  //   throw std::invalid_argument("Invalid zk path.");
}

void ZkClient::updateCommand(int rc, CommandToken *tok) {
  if (rc) {
    tok->setError(rc);
  } else {
    tok->addRef();
    tok->attach(this);
    processRequest(0);
  }
}

void ZkClient::addWatch(CommandToken *tok, const std::string &path) {
  Promise<WatchedEvent> p;
  tok->setWatch(p.getFuture());
  watchers_.insert(std::make_pair(path, std::move(p)));
}

void ZkClient::processRequest(int revents) {
  int rev = 0;
  if (revents & ev::READ)
      rev |= ZOOKEEPER_READ;
  if (revents & ev::WRITE)
      rev |= ZOOKEEPER_WRITE;
  int rc = zookeeper_process(zh_, rev);
  if (rc) {
    FUTURES_DLOG(ERROR) << "ZooKeeper error: " << rc;
    // fatal errors
    if (rc == ZSESSIONEXPIRED) {
      watchers_.clear();
      throw ZookeeperException("zookeeper_process", rc);
    }
  }
  if (revents) updateWatcher();
}

io::intrusive_ptr<ConnectToken> ZkClient::doConnect() {
  io::intrusive_ptr<ConnectToken> tok(new ConnectToken());
  if (zoo_state(zh_) == ZOO_CONNECTED_STATE)
    tok->notifyDone();
  else
    tok->attach(this);
  return tok;
}

io::intrusive_ptr<CommandToken> ZkClient::doGetChildren(const std::string &path, bool watch)
{
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::GET_CHILDREN));
  int rc = zoo_aget_children(zh_, path.c_str(), watch, ZkClient::onStringsCompletion, tok.get());
  if (!rc && watch) addWatch(tok.get(), path);
  updateCommand(rc, tok.get());
  return tok;
}

io::intrusive_ptr<CommandToken> ZkClient::doGetChildren2(const std::string &path, bool watch)
{
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::GET_CHILDREN2));
  int rc = zoo_aget_children2(zh_, path.c_str(), watch, ZkClient::onStringsStatCompletion, tok.get());
  if (!rc && watch) addWatch(tok.get(), path);
  updateCommand(rc, tok.get());
  return tok;
}

io::intrusive_ptr<CommandToken> ZkClient::doGet(const std::string &path, bool watch) {
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::GET));
  int rc = zoo_aget(zh_, path.c_str(), watch, ZkClient::onDataCompletion, tok.get());
  if (!rc && watch) addWatch(tok.get(), path);
  updateCommand(rc, tok.get());
  return tok;

}

io::intrusive_ptr<CommandToken> ZkClient::doSet(const std::string &path,
    const char *buffer, int len, int version) {
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::SET));
  if (len > kMaxDataSize) {
    tok->setError(ZBADARGUMENTS);
    return tok;
  }
  int rc = zoo_aset(zh_, path.c_str(), buffer, len, version, ZkClient::onStatCompletion, tok.get());
  updateCommand(rc, tok.get());
  return tok;
}


io::intrusive_ptr<CommandToken> ZkClient::doCreate(const std::string &path,
  const char *value, int valuelen,
  const struct ACL_vector *acl, int flags)
{
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::CREATE));
  int zf = 0;
  if (flags & CreateFlags::kEphemeral) zf |= ZOO_EPHEMERAL;
  if (flags & CreateFlags::kSequence) zf |= ZOO_SEQUENCE;

  if (valuelen > kMaxDataSize) {
    tok->setError(ZBADARGUMENTS);
    return tok;
  }

  int rc = zoo_acreate(zh_, path.c_str(), value, valuelen, &ZOO_OPEN_ACL_UNSAFE, zf, ZkClient::onStringCompletion, tok.get());
  updateCommand(rc, tok.get());
  return tok;
}

io::intrusive_ptr<CommandToken> ZkClient::doDelete(const std::string &path, int version)
{
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::DELETE));
  int rc = zoo_adelete(zh_, path.c_str(), version, ZkClient::onVoidCompletion, tok.get());
  updateCommand(rc, tok.get());
  return tok;
}

io::intrusive_ptr<CommandToken> ZkClient::doExists(const std::string &path, bool watch)
{
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::DELETE));
  int rc = zoo_aexists(zh_, path.c_str(), watch, ZkClient::onStatCompletion, tok.get());
  if (!rc && watch) addWatch(tok.get(), path);
  updateCommand(rc, tok.get());
  return tok;
}

io::intrusive_ptr<CommandToken> ZkClient::doSync(const std::string &path) {
  checkPath(path);
  io::intrusive_ptr<CommandToken> tok(new CommandToken(CommandToken::SYNC));
  int rc = zoo_async(zh_, path.c_str(), ZkClient::onStringCompletion, tok.get());
  updateCommand(rc, tok.get());
  return tok;
}

io::intrusive_ptr<EventStreamToken> ZkClient::doEventStream() {
  if (!getPending(io::IOObject::OpRead).empty())
    throw std::invalid_argument("already subscribing");
  io::intrusive_ptr<EventStreamToken> tok(new EventStreamToken());
  tok->attach(this);
  return tok;
}

ConnectFuture ZkClient::waitConnect() {
  return ConnectFuture(shared_from_this());
}

GetChildrenCommandFuture ZkClient::getChildren(const std::string &path)
{
  return GetChildrenCommandFuture(shared_from_this(), path);
}

GetChildrenWCommandFuture ZkClient::getChildrenW(const std::string &path)
{
  return GetChildrenWCommandFuture(shared_from_this(), path);
}

GetChildren2CommandFuture ZkClient::getChildren2(const std::string &path)
{
  return GetChildren2CommandFuture(shared_from_this(), path);
}

GetChildren2WCommandFuture ZkClient::getChildren2W(const std::string &path)
{
  return GetChildren2WCommandFuture(shared_from_this(), path);
}

GetCommandFuture ZkClient::getData(const std::string &path)
{
  return GetCommandFuture(shared_from_this(), path);
}

GetWCommandFuture ZkClient::getDataW(const std::string &path)
{
  return GetWCommandFuture(shared_from_this(), path);
}

SetCommandFuture ZkClient::setData(const std::string &path, const std::string &data, int version)
{
  return SetCommandFuture(shared_from_this(), path, data, version);
}

CreateCommandFuture ZkClient::createNode(const std::string &path, const std::string &data,
            const struct ACL_vector *acl, int flags)
{
  return CreateCommandFuture(shared_from_this(), path, data, acl, flags);
}

DeleteCommandFuture ZkClient::deleteNode(const std::string &path, int version)
{
  return DeleteCommandFuture(shared_from_this(), path, version);
}

ExistsCommandFuture ZkClient::existsNode(const std::string &path)
{
  return ExistsCommandFuture(shared_from_this(), path);
}

ExistsWCommandFuture ZkClient::existsNodeW(const std::string &path) {
  return ExistsWCommandFuture(shared_from_this(), path);
}

SyncCommandFuture ZkClient::syncNode(const std::string &path)
{
  return SyncCommandFuture(shared_from_this(), path);
}

ZkEventStream ZkClient::eventStream()
{
  return ZkEventStream(shared_from_this());
}

}
}

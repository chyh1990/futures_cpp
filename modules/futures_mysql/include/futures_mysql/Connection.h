#pragma once

#include <futures/EventExecutor.h>
#include <futures_mysql/Exception.h>
#include <futures/Future.h>
#include <futures_mysql/MySql.h>
#include <futures_mysql/ResultSet.h>
#include <futures_mysql/PreparedStatement.h>
#include <futures_mysql/Command.h>

namespace futures {
namespace mysql {

struct Config {
  std::string host;
  uint16_t port;
  std::string user;
  std::string passwd;
  std::string schema;
};

class PreparedStatement;

namespace detail {

class ConnFuture;
class ExecFuture;
class DropExecFuture;
class StmtFuture;
class ExecStream;

}

class Connection : public io::IOObject,
  public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;

    Connection(EventExecutor* ev, const Config &c);
    ~Connection();

    void onCancel(CancelReason r) override;

    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    io::intrusive_ptr<ConnectRequest> doConnect();

    io::intrusive_ptr<WriteCommandRequest> doCommand(WriteCommandRequest::Type type, const std::string &query, bool has_result);
    io::intrusive_ptr<WriteCommandRequest> doStmtCommand(WriteCommandRequest::Type type, PreparedStatement::Ptr stmt, bool has_result);

    bool good() const {
      return ret_ && (s_ != CLOSED)
        && (s_ != CLOSE_START)
        && (s_ != CLOSE_CONT)
        && (s_ != CLOSE_DONE);
    }

    bool isIdle() const {
      return s_ == ESTABLISHED;
    }

    size_t getErrors() const {
      return errors_;
    }

    ev::tstamp getLastUsedTimestamp() const {
      return last_used_;
    }

    MYSQL* getRaw() {
      return &inst_;
    }
    MySqlError getLastMySqlError();

    std::string escapeString(const std::string &s);

    // future API
    static detail::ConnFuture connect(EventExecutor *ev, const Config &config);
    detail::ExecFuture exec(const std::string &q);
    detail::ExecFuture query(const std::string &q);
    detail::StmtFuture prepare(const std::string &q);

    detail::DropExecFuture transaction();
    detail::DropExecFuture rollback();
    detail::DropExecFuture commit();
    detail::DropExecFuture close();

    detail::ExecStream queryStream(const std::string &q);
private:
    enum State {
      CLOSED,
      CLOSE_START,
      CLOSE_CONT,
      CLOSE_DONE,
      CONNECTING,
      CONNECTED,
      ESTABLISHED,
      NEXT_COMMAND,
      QUERY_START,
      QUERY_CONT,
      PREPARE_STMT_START,
      PREPARE_STMT_CONT,
      PREPARE_STMT_DONE,
      PREPARE_STMT_EXEC_START,
      PREPARE_STMT_EXEC_CONT,
      PREPARE_STMT_EXEC_DONE,
      PREPARE_STMT_CLOSE_START,
      PREPARE_STMT_CLOSE_CONT,
      PREPARE_STMT_CLOSE_DONE,
      USE_RESULT,
      FETCH_ROW_START,
      FETCH_ROW_CONT,
      FETCH_ROW_DONE,
    };
    State s_ = CLOSED;
    ev::io io_;
    ev::timer timer_;
    const Config config_;
    size_t errors_ = 0;
    ev::tstamp last_used_ = 0;

    MYSQL inst_;
    MYSQL *ret_ = nullptr;
    int err_ = 0;
    my_bool berr_ = 0;
    MYSQL_RES *result_ = nullptr;
    MYSQL_ROW row_ = nullptr;

    io::intrusive_ptr<WriteCommandRequest> current_;

    void onEvent(ev::io &watcher, int revent);
    void onTimout(ev::timer &watcher, int revent);
    int stateMachine(int revent);
    void nextEvent(State new_st, int status);

    void finishAllConnects(const MySqlError &err);
    void finishAllWrites(const MySqlError& err);
    void closeOnError(const MySqlError &err);

    WriteCommandRequest *getFrontWrite() {
      auto &l = getPending(io::IOObject::OpWrite);
      if (l.empty()) return nullptr;
      return static_cast<WriteCommandRequest*>(&l.front());
    }

    void finishCurrentQuery(const MySqlError& err);
};

namespace detail {

class ConnFuture : public FutureBase<ConnFuture, Connection::Ptr> {
public:
  using Item = Connection::Ptr;

  ConnFuture(EventExecutor *ev, Config config)
    : c_(std::make_shared<Connection>(ev, config)) {}

  Poll<Item> poll() override {
    if (!tok_)
      tok_ = c_->doConnect();
    switch (tok_->getState()) {
    case io::CompletionToken::STARTED:
      tok_->park();
      return Poll<Item>(not_ready);
    case io::CompletionToken::CANCELLED:
      return Poll<Item>(FutureCancelledException());
    case io::CompletionToken::DONE:
      if (tok_->getError().good()) {
        return makePollReady(std::move(c_));
      } else {
        return Poll<Item>(MySqlException(tok_->getError()));
      }
    }
  }

private:
  Connection::Ptr c_;
  io::intrusive_ptr<ConnectRequest> tok_;
};

class ExecFuture : public FutureBase<ExecFuture, ResultSet> {
public:
  using Item = ResultSet;

  ExecFuture(Connection::Ptr c, WriteCommandRequest::Type type,
      const std::string &q, bool has_result)
    : c_(c), type_(type), q_(q), has_result_(has_result) {
  }

  Poll<Item> poll() override {
    if (!tok_)
      tok_ = c_->doCommand(type_, std::move(q_), has_result_);
    switch (tok_->getState()) {
    case io::CompletionToken::STARTED:
      tok_->park();
      return Poll<Item>(not_ready);
    case io::CompletionToken::CANCELLED:
      return Poll<Item>(FutureCancelledException());
    case io::CompletionToken::DONE:
      if (tok_->getError().good()) {
        return makePollReady(std::move(tok_->getResult()));
      } else {
        return Poll<Item>(MySqlException(tok_->getError()));
      }
    }
  }

private:
  Connection::Ptr c_;
  WriteCommandRequest::Type type_;
  std::string q_;
  bool has_result_;
  io::intrusive_ptr<WriteCommandRequest> tok_;
};

class DropExecFuture : public FutureBase<DropExecFuture, folly::Unit> {
public:
  using Item = folly::Unit;

  DropExecFuture(Connection::Ptr c, const std::string &q, WriteCommandRequest::Type type = WriteCommandRequest::Query)
    : exec_(c, type, q, false) {
  }

  Poll<Item> poll() override {
    auto r = exec_.poll();
    if (r.hasException())
      return Poll<Item>(r.exception());
    if (r->hasValue())
      return makePollReady(folly::unit);
    return Poll<Item>(not_ready);
  }

private:
  ExecFuture exec_;
};

class StmtFuture : public FutureBase<StmtFuture, PreparedStatement::Ptr> {
public:
  using Item = PreparedStatement::Ptr;

  StmtFuture(Connection::Ptr c, const std::string &q)
    : c_(c), q_(q) {
  }

  Poll<Item> poll() override {
    if (!tok_)
      tok_ = c_->doCommand(WriteCommandRequest::PrepareStmt,
          std::move(q_), false);
    switch (tok_->getState()) {
    case io::CompletionToken::STARTED:
      tok_->park();
      return Poll<Item>(not_ready);
    case io::CompletionToken::CANCELLED:
      return Poll<Item>(FutureCancelledException());
    case io::CompletionToken::DONE:
      if (tok_->getError().good()) {
        return makePollReady(tok_->moveStatement());
      } else {
        return Poll<Item>(MySqlException(tok_->getError()));
      }
    }
  }

private:
  Connection::Ptr c_;
  std::string q_;
  io::intrusive_ptr<WriteCommandRequest> tok_;
};


}

}
}

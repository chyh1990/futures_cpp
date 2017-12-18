#pragma once

#include <deque>
#include <futures_mysql/Exception.h>
#include <futures_mysql/ResultSet.h>
#include <futures/io/WaitHandleBase.h>

namespace futures {
namespace mysql {

class Connection;
class PreparedStatement;

class ConnectRequest : public io::CompletionToken {
public:
  ConnectRequest() : io::CompletionToken(io::IOObject::OpConnect) {}
  ~ConnectRequest() {
    cleanup(CancelReason::UserCancel);
  }

  void setError(const MySqlError &err) {
    error_ = err;
    notifyDone();
  }

  void onCancel(CancelReason r) override {}

  const MySqlError &getError() const {
    return error_;
  }
private:
  MySqlError error_;
};

class WriteCommandRequest : public io::CompletionToken {
public:
  enum Type {
    Query,
    PrepareStmt,
    StmtExec,
    StmtClose,
    ConnClose,
  };

  WriteCommandRequest(Type type, const std::string &command,
      bool has_result, bool streaming = false)
    : io::CompletionToken(io::IOObject::OpWrite),
      type_(type), query_(command),
      has_result_(has_result), streaming_(streaming)
  {}

  // PS streaming result not supported
  WriteCommandRequest(Type type, std::shared_ptr<PreparedStatement> stmt, bool has_result)
    : io::CompletionToken(io::IOObject::OpWrite),
      type_(type), has_result_(has_result), stmt_(stmt), streaming_(false)
  {}

  void onCancel(CancelReason r) override {}

  Type getType() const { return type_; }
  const std::string& getQuery() const { return query_; }

  void setError(const MySqlError &err) {
    error_ = err;
    notifyDone();
  }

  const MySqlError &getError() const {
    return error_;
  }

  ResultSet &getResult() {
    return cached_result_;
  }

  const ResultSet &getResult() const {
    return cached_result_;
  }

  void createStatement(Connection *conn) {
    stmt_ = std::make_shared<PreparedStatement>(conn);
  }

  PreparedStatement* getStatement() {
    return stmt_.get();
  }

  const PreparedStatement *getStatement() const {
    return stmt_.get();
  }

  std::shared_ptr<PreparedStatement> moveStatement() {
    return std::move(stmt_);
  }

  bool hasRowResult() const {
    return has_result_;
  }

  void addRow(MYSQL_ROW row) {
    if (drop_) return;
    if (!streaming_) {
      cached_result_.addRow(row);
    } else {
      rows_.push_back(Row(cached_result_.getFieldsPtr(), row));
      notify();
    }
  }

  void setDrop() { drop_ = true; }
private:
  const Type type_;
  const std::string query_;
  const bool has_result_;
  std::shared_ptr<PreparedStatement> stmt_;
  const bool streaming_;
  bool drop_ = false;

  // results
  MySqlError error_;
  ResultSet cached_result_;
  std::deque<Row> rows_;
};


}
}

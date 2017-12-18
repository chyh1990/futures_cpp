#pragma once

#include <futures_mysql/MySql.h>
#include <futures_mysql/Connection.h>

namespace futures {
namespace mysql {

class Connection;
class TxFuture;

class Transaction : std::enable_shared_from_this<Transaction> {
public:
    Transaction(Connection *conn_);
    ~Transaction();

    TxFuture rollback();
    TxFuture commit();
private:
    bool started_ = true;
    Connection *conn_;
};

class TxFuture : public FutureBase<TxFuture, folly::Unit> {
public:
  using Item = folly::Unit;

  TxFuture(std::shared_ptr<Connection> c, bool commit);

  Poll<Item> poll();

private:
  std::shared_ptr<Connection> c_;
  bool commit_;
  io::intrusive_ptr<WriteCommandRequest> tok_;
};



}
}

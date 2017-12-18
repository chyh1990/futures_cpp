#pragma once

#include <vector>
#include <futures_mysql/Exception.h>
#include <futures_mysql/MySql.h>
#include <futures_mysql/ResultSet.h>
#include <futures_mysql/Command.h>
#include <futures_mysql/SqlTypes.h>

namespace futures {
namespace mysql {

class Connection;

class BindingBuffer {
public:
    BindingBuffer() {
    }

    void reset(size_t count) {
        values_.resize(count);
        binds_.resize(count);
        clear();
        FUTURES_DLOG(INFO) << "reset bind: " << values_.size();
    }

    template <typename V>
    void set(size_t idx, V&& v) {
        assert(idx < values_.size());
        values_[idx].set<typename std::decay<V>::type>(std::forward<V>(v));
    }

    void clear() {
        for(size_t i = 0; i < values_.size(); ++i)
            values_[i].set<NullType>(NullType());
    }

    MYSQL_BIND *fillBinds() {
        memset(&binds_[0], 0, binds_.size() * sizeof(MYSQL_BIND));
#define _CASE_INT(cpptype, mytype) \
    } else if (values_[i].is<cpptype>()) { \
      binds_[i].buffer_type = MYSQL_TYPE_ ## mytype; \
      binds_[i].buffer = (char*)&values_[i].get<cpptype>(); \

        for (size_t i = 0; i < values_.size(); ++i) {
            if (values_[i].is<NullType>()) {
                binds_[i].buffer_type = MYSQL_TYPE_NULL;
            _CASE_INT(TinyType, TINY)
            _CASE_INT(ShortType, SHORT)
            _CASE_INT(LongType, LONG)
            _CASE_INT(LongLongType, LONGLONG)
            } else if (values_[i].is<StringType>()) {
                binds_[i].buffer_type = MYSQL_TYPE_STRING;
                binds_[i].buffer = (char*)&values_[i].get<StringType>()[0];
                binds_[i].buffer_length = values_[i].get<StringType>().size();
            } else if (values_[i].is<BlobType>()) {
                binds_[i].buffer_type = MYSQL_TYPE_STRING;
                binds_[i].buffer = (char*)&values_[i].get<BlobType>()[0];
                binds_[i].buffer_length = values_[i].get<BlobType>().size();
            } else {
                throw std::invalid_argument("unsupported ps datatype");
                // binds_[i].buffer_type = MYSQL_TYPE_NULL;
                // binds_[i].is_null = (char *)&is_null;
            }
        }
        return &binds_[0];
    }

private:
    my_bool is_null = 1;
    std::vector<CellDataType> values_;
    std::vector<MYSQL_BIND> binds_;
};

namespace detail {
class StmtCloseFuture;
class StmtExecFuture;
}

class PreparedStatement : public std::enable_shared_from_this<PreparedStatement> {
public:
    using Ptr = std::shared_ptr<PreparedStatement>;

    PreparedStatement(Connection* conn);
    ~PreparedStatement();

    MYSQL_STMT *getRaw() { return stmt_; }

    PreparedStatement(const PreparedStatement&) = delete;
    PreparedStatement& operator=(const PreparedStatement&) = delete;
    PreparedStatement(PreparedStatement&& o) = delete;
    PreparedStatement& operator=(PreparedStatement&& o) = delete;

    size_t getParamCount() const;
    size_t getAffectedRows() const;
    size_t getInsertId() const;
    MySqlError getLastMySqlError();

    template <typename V>
    void set(size_t idx, V&& v) {
        buffer_.set(idx, std::forward<V>(v));
    }

    io::intrusive_ptr<WriteCommandRequest> doCommand(WriteCommandRequest::Type type, bool has_result);

    detail::StmtCloseFuture close();
    detail::StmtExecFuture exec();

private:
    Connection *conn_;
    MYSQL_STMT* stmt_;
    BindingBuffer buffer_;

    void resetBind() {
        buffer_.reset(getParamCount());
    }

    void bind();

    void forceClose();
    void markClosed();


    friend Connection;
};

namespace detail {

class StmtCloseFuture: public FutureBase<StmtCloseFuture, folly::Unit> {
public:
  using Item = folly::Unit;

  StmtCloseFuture(PreparedStatement::Ptr p)
        :s_(p) {
  }

  Poll<Item> poll() override {
    if (!tok_)
      tok_ = s_->doCommand(WriteCommandRequest::StmtClose, false);
    switch (tok_->getState()) {
    case io::CompletionToken::STARTED:
      tok_->park();
      return Poll<Item>(not_ready);
    case io::CompletionToken::CANCELLED:
      return Poll<Item>(FutureCancelledException());
    case io::CompletionToken::DONE:
      if (tok_->getError().good()) {
        return makePollReady(folly::unit);
      } else {
        return Poll<Item>(MySqlException(tok_->getError()));
      }
    }
  }

private:
  PreparedStatement::Ptr s_;
  io::intrusive_ptr<WriteCommandRequest> tok_;
};

class StmtExecFuture: public FutureBase<StmtExecFuture, ResultSet> {
public:
  using Item = ResultSet;

  StmtExecFuture(PreparedStatement::Ptr s)
        : s_(s) {
  }

  Poll<Item> poll() override {
    if (!tok_)
      tok_ = s_->doCommand(WriteCommandRequest::StmtExec, false);
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
  PreparedStatement::Ptr s_;
  io::intrusive_ptr<WriteCommandRequest> tok_;
};


}


}
}

#include <futures_mysql/Connection.h>
#include <futures_mysql/PreparedStatement.h>

namespace futures {
namespace mysql {


PreparedStatement::PreparedStatement(Connection* conn)
    : conn_(conn), stmt_(nullptr)
{
    stmt_ = mysql_stmt_init(conn->getRaw());
    if (!stmt_)
        throw MySqlException(conn->getLastMySqlError());
}

void PreparedStatement::forceClose() {
    if (stmt_) {
        FUTURES_DLOG(INFO) << "force close";
        mysql_stmt_close(stmt_);
    }
    markClosed();
}

void PreparedStatement::markClosed() {
    stmt_ = nullptr;
    conn_ = nullptr;
}

MySqlError PreparedStatement::getLastMySqlError() {
    if (mysql_stmt_errno(stmt_)) {
        return MySqlError(mysql_stmt_errno(stmt_), mysql_stmt_error(stmt_));
    } else {
        return MySqlError();
    }
}

size_t PreparedStatement::getParamCount() const {
    return stmt_ ? mysql_stmt_param_count(stmt_) : 0;
}

size_t PreparedStatement::getAffectedRows() const {
    return stmt_ ? mysql_stmt_affected_rows(stmt_) : 0;
}

size_t PreparedStatement::getInsertId() const {
    return stmt_ ? mysql_stmt_insert_id(stmt_) : 0;
}

void PreparedStatement::bind() {
    FUTURES_CHECK (stmt_);
    int err = mysql_stmt_bind_param(stmt_, buffer_.fillBinds());
    if (err)
        throw MySqlException(err, "Failed to bind statement.");
}

PreparedStatement::~PreparedStatement() {
    forceClose();
}

io::intrusive_ptr<WriteCommandRequest> PreparedStatement::doCommand(WriteCommandRequest::Type type, bool has_result) {
    FUTURES_CHECK (stmt_);
    FUTURES_CHECK (conn_);

    return conn_->doStmtCommand(type, shared_from_this(), has_result);
}

detail::StmtCloseFuture PreparedStatement::close() {
    return detail::StmtCloseFuture(shared_from_this());
}

detail::StmtExecFuture PreparedStatement::exec() {
    return detail::StmtExecFuture(shared_from_this());
}

}
}


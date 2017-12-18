#include <futures/EventExecutor.h>
#include <futures_mysql/Connection.h>
#include <futures/Core.h>

namespace futures {
namespace mysql {

Connection::Connection(EventExecutor *ev, const Config &c)
      : io::IOObject(ev),
        io_(ev->getLoop()), timer_(ev->getLoop()), config_(c) {
    mysql_init(&inst_);
    mysql_options(&inst_, MYSQL_OPT_NONBLOCK, nullptr);
    io_.set<Connection, &Connection::onEvent>(this);
    timer_.set<Connection, &Connection::onTimout>(this);
}

Connection::~Connection() {
    if (ret_) {
        closeOnError(MySqlError(-1, "Connection destroy"));
    }
}

io::intrusive_ptr<ConnectRequest> Connection::doConnect() {
    if (s_ != CLOSED)
        throw MySqlException("already connecting");
    io::intrusive_ptr<ConnectRequest> p(new ConnectRequest());
    p->attach(this);
    stateMachine(0);
    return p;
}

io::intrusive_ptr<WriteCommandRequest>
Connection::doCommand(WriteCommandRequest::Type type, const std::string &s, bool has_result) {
    io::intrusive_ptr<WriteCommandRequest> p(
            new WriteCommandRequest(type, s, has_result));
    if (!good()) {
        p->setError(MySqlError(-1, "Connection closed."));
    } else {
        p->attach(this);
        if (s_ == ESTABLISHED)
            stateMachine(0);
    }
    return p;
}

io::intrusive_ptr<WriteCommandRequest>
Connection::doStmtCommand(WriteCommandRequest::Type type, PreparedStatement::Ptr stmt, bool has_result) {
    io::intrusive_ptr<WriteCommandRequest> p(
            new WriteCommandRequest(type, stmt, has_result));
    if (!good()) {
        p->setError(MySqlError(-1, "Connection closed."));
    } else {
        p->attach(this);
        if (s_ == ESTABLISHED)
            stateMachine(0);
    }
    return p;
}

static int
mysql_status(short event)
{
    int status= 0;
    if (event & ev::READ)
        status|= MYSQL_WAIT_READ;
    if (event & ev::WRITE)
        status|= MYSQL_WAIT_WRITE;
    if (event & ev::TIMER)
        status|= MYSQL_WAIT_TIMEOUT;
    return status;
}

#define NEXT_IMMEDIATE(new_st) do { s_ = new_st; goto again; } while (0)
#define GOTO_AND_RETURN(new_st) do { s_ = new_st; return 0; } while(0)

int Connection::stateMachine(int revent) {
    int status = 0;
    WriteCommandRequest *req = nullptr;

    if (revent)
        last_used_ = getExecutor()->getNow();
again:
    // FUTURES_DLOG(INFO) << "State: " << s_ << ", ev: " << revent;
    switch (s_) {
    case CLOSED:
        status = mysql_real_connect_start(&ret_, &inst_, config_.host.c_str(),
                config_.user.c_str(), config_.passwd.c_str(),
                config_.schema.c_str(), config_.port, nullptr, 0);
        if (status) {
            nextEvent(CONNECTING, status);
        } else {
            NEXT_IMMEDIATE(CONNECTED);
        }
        break;
    case CONNECTING:
        status = mysql_real_connect_cont(&ret_, &inst_, mysql_status(revent));
        if (status)
            nextEvent(CONNECTING, status);
        else
            NEXT_IMMEDIATE(CONNECTED);
        break;
    case CONNECTED:
        if (!ret_) {
            FUTURES_DLOG(INFO) << "mysql connect failed";
            errors_++;
            closeOnError(getLastMySqlError());
            return -1;
        }
        FUTURES_DLOG(INFO) << "mysql connected";
        finishAllConnects(MySqlError());
        NEXT_IMMEDIATE(ESTABLISHED);
        break;
    case ESTABLISHED:
        req = getFrontWrite();
        if (!req) {
            // io_.stop();
            return 0;
        } else {
            assert(!current_);
            req->addRef();
            current_ = req;
            if (req->getType() == WriteCommandRequest::Query) {
                NEXT_IMMEDIATE(QUERY_START);
            } else if (req->getType() == WriteCommandRequest::PrepareStmt) {
                NEXT_IMMEDIATE(PREPARE_STMT_START);
            } else if (req->getType() == WriteCommandRequest::StmtExec) {
                NEXT_IMMEDIATE(PREPARE_STMT_EXEC_START);
            } else if (req->getType() == WriteCommandRequest::StmtClose) {
                NEXT_IMMEDIATE(PREPARE_STMT_CLOSE_START);
            } else if (req->getType() == WriteCommandRequest::ConnClose) {
                NEXT_IMMEDIATE(CLOSE_START);
            } else {
                assert(0 && "unsupported");
            }
        }
        break;
    case QUERY_START:
        assert(current_->getType() == WriteCommandRequest::Query);
        FUTURES_DLOG(INFO) << "start query: " << current_->getQuery();
        status = mysql_real_query_start(&err_, &inst_, current_->getQuery().c_str(), current_->getQuery().size());
        if (status)
            nextEvent(QUERY_CONT, status);
        else
            NEXT_IMMEDIATE(USE_RESULT);
        break;
    case QUERY_CONT:
        status = mysql_real_query_cont(&err_, &inst_, mysql_status(revent));
        if (status)
            nextEvent(QUERY_CONT, status);
        else
            NEXT_IMMEDIATE(USE_RESULT);
        break;
    case PREPARE_STMT_START:
        assert(current_->getType() == WriteCommandRequest::PrepareStmt);
        FUTURES_DLOG(INFO) << "start prepare_stmt: " << current_->getQuery();
        current_->createStatement(this);
        status = mysql_stmt_prepare_start(&err_,
                current_->getStatement()->getRaw(),
                current_->getQuery().c_str(), current_->getQuery().size());
        if (status)
            nextEvent(PREPARE_STMT_CONT, status);
        else
            NEXT_IMMEDIATE(PREPARE_STMT_DONE);
        break;
    case PREPARE_STMT_CONT:
        status = mysql_stmt_prepare_cont(&err_,
                current_->getStatement()->getRaw(),
                mysql_status(revent));
        if (status)
            nextEvent(PREPARE_STMT_CONT, status);
        else
            NEXT_IMMEDIATE(PREPARE_STMT_DONE);
        break;
    case PREPARE_STMT_DONE:
        if (err_) {
            FUTURES_DLOG(ERROR) << "prepare statment failed: " << mysql_error(&inst_);
            MySqlError err(current_->getStatement()->getLastMySqlError());
            current_->getStatement()->forceClose();
            finishCurrentQuery(err);
        } else {
            current_->getStatement()->resetBind();
            finishCurrentQuery(MySqlError());
            /*
            auto r = mysql_stmt_result_metadata(current_->getStatement()->getRaw());
            ResultSet rr(r);
            rr.dump(std::cerr);
            mysql_free_result(r);
            */
        }
        NEXT_IMMEDIATE(ESTABLISHED);
        break;
    case PREPARE_STMT_CLOSE_START:
        assert(current_->getType() == WriteCommandRequest::StmtClose);
        FUTURES_DLOG(INFO) << "close prepare_stmt: " << current_->getQuery();
        status = mysql_stmt_close_start(&berr_, current_->getStatement()->getRaw());
        if (status)
            nextEvent(PREPARE_STMT_CLOSE_CONT, status);
        else
            NEXT_IMMEDIATE(PREPARE_STMT_CLOSE_DONE);
        break;
    case PREPARE_STMT_CLOSE_CONT:
        status = mysql_stmt_close_cont(&berr_, current_->getStatement()->getRaw(),
                mysql_status(revent));
        if (status)
            nextEvent(PREPARE_STMT_CLOSE_CONT, status);
        else
            NEXT_IMMEDIATE(PREPARE_STMT_CLOSE_DONE);
        break;
    case PREPARE_STMT_CLOSE_DONE:
        if (berr_) {
            FUTURES_LOG(ERROR) << "close statement failed";
            MySqlError err(current_->getStatement()->getLastMySqlError());
            current_->getStatement()->forceClose();
            finishCurrentQuery(err);
        } else {
            FUTURES_DLOG(INFO) << "statement closed";
            current_->getStatement()->markClosed();
            finishCurrentQuery(MySqlError());
        }
        NEXT_IMMEDIATE(ESTABLISHED);
        break;
    case USE_RESULT:
        if (err_) {
            FUTURES_DLOG(ERROR) << "query failed: " << mysql_error(&inst_);
            errors_++;
            finishCurrentQuery(getLastMySqlError());
            NEXT_IMMEDIATE(ESTABLISHED);
        } else {
            current_->getResult().setAffectedRows(
                    mysql_affected_rows(&inst_));
            current_->getResult().setInsertId(
                    mysql_insert_id(&inst_));
            if (current_->hasRowResult()) {
                result_ = mysql_use_result(&inst_);
                if (!result_) {
                    FUTURES_LOG(FATAL) << "mysql_use_result() returns error: " << mysql_errno(&inst_);
                }
                current_->getResult().set(result_);
                NEXT_IMMEDIATE(FETCH_ROW_START);
            } else {
                finishCurrentQuery(getLastMySqlError());
                NEXT_IMMEDIATE(ESTABLISHED);
            }
        }
        break;
    case PREPARE_STMT_EXEC_START:
        assert(current_->getType() == WriteCommandRequest::StmtExec);
        FUTURES_DLOG(INFO) << "start stmt exec: " << current_->getQuery();
        current_->getStatement()->bind();
        status = mysql_stmt_execute_start(&err_,
                current_->getStatement()->getRaw());
        if (status)
            nextEvent(PREPARE_STMT_EXEC_CONT, status);
        else
            NEXT_IMMEDIATE(PREPARE_STMT_EXEC_CONT);
        break;
    case PREPARE_STMT_EXEC_CONT:
        status = mysql_stmt_execute_cont(&err_,
                current_->getStatement()->getRaw(),
                mysql_status(revent));
        if (status)
            nextEvent(PREPARE_STMT_EXEC_CONT, status);
        else
            NEXT_IMMEDIATE(PREPARE_STMT_EXEC_DONE);
        break;
    case PREPARE_STMT_EXEC_DONE:
        if (err_) {
            FUTURES_DLOG(ERROR) << "stmt exec failed: " << mysql_error(&inst_);
            errors_++;
        } else {
            current_->getResult()
                .setAffectedRows(current_->getStatement()->getAffectedRows());
            current_->getResult()
                .setInsertId(current_->getStatement()->getInsertId());
        }
        finishCurrentQuery(current_->getStatement()->getLastMySqlError());
        NEXT_IMMEDIATE(ESTABLISHED);
        break;
    case FETCH_ROW_START:
        assert(result_);
        status = mysql_fetch_row_start(&row_, result_);
        if (status)
            nextEvent(FETCH_ROW_CONT, status);
        else
            NEXT_IMMEDIATE(FETCH_ROW_DONE);
        break;
    case FETCH_ROW_CONT:
        status= mysql_fetch_row_cont(&row_, result_, mysql_status(revent));
        if (status)
            nextEvent(FETCH_ROW_CONT, status);
        else
            NEXT_IMMEDIATE(FETCH_ROW_DONE);
        break;
    case FETCH_ROW_DONE:
        if (row_) {
            current_->getResult().addRow(row_);
            NEXT_IMMEDIATE(FETCH_ROW_START);
        } else {
            if (mysql_errno(&inst_)) {
                errors_++;
                FUTURES_DLOG(ERROR) << "failed to fetch row: " << mysql_error(&inst_);
                finishCurrentQuery(getLastMySqlError());
            } else {
                // EOF
                // current_->getResult().dump(std::cerr);
                finishCurrentQuery(MySqlError());
            }
            NEXT_IMMEDIATE(ESTABLISHED);
        }
        break;
    case CLOSE_START:
        assert(current_->getType() == WriteCommandRequest::ConnClose);
        FUTURES_DLOG(INFO) << "start conn close";
        status = mysql_close_start(&inst_);
        if (status)
            nextEvent(CLOSE_CONT, status);
        else
            NEXT_IMMEDIATE(CLOSE_DONE);
        break;
    case CLOSE_CONT:
        status = mysql_close_cont(&inst_, mysql_status(revent));
        if (status)
            nextEvent(CLOSE_CONT, status);
        else
            NEXT_IMMEDIATE(CLOSE_DONE);
        break;
    case CLOSE_DONE:
        FUTURES_DLOG(INFO) << "connection closed";
        finishCurrentQuery(MySqlError());
        ret_ = nullptr;
        GOTO_AND_RETURN(CLOSED);
        break;
    default:
        FUTURES_CHECK (false) << "invalid state: " << s_;
    }

    return status;
}

void Connection::nextEvent(State new_st, int status) {
    short wait_event= 0;
    unsigned int timeout = 0;
    int fd = -1;
    if (status & MYSQL_WAIT_READ)
        wait_event|= ev::READ;
    if (status & MYSQL_WAIT_WRITE)
        wait_event|= ev::WRITE;
    if (wait_event)
        fd= mysql_get_socket(&inst_);
    if (status & MYSQL_WAIT_TIMEOUT) {
        timeout = mysql_get_timeout_value(&inst_);
    }
    if (fd != -1 && wait_event) {
        io_.set(fd, wait_event);
        io_.start();
    } else {
        io_.stop();
    }
    //TODO TIMEOUT
    if (timeout) {
        timer_.start(timeout);
    } else {
        timer_.stop();
    }

    s_ = new_st;
}

void Connection::onEvent(ev::io &watcher, int revent) {
    if ((revent & ev::READ) || (revent & ev::WRITE))
        stateMachine(revent);
}

void Connection::onTimout(ev::timer &watcher, int revent) {
    if (revent & ev::TIMER)
        stateMachine(revent);
}

void Connection::finishAllConnects(const MySqlError &err) {
    auto &conns = getPending(io::IOObject::OpConnect);
    while (!conns.empty())
        static_cast<ConnectRequest*>(&conns.front())->setError(err);
}

void Connection::finishAllWrites(const MySqlError &err) {
    finishCurrentQuery(err);
    auto &writes = getPending(io::IOObject::OpWrite);
    while (!writes.empty())
        static_cast<WriteCommandRequest*>(&writes.front())->setError(err);
}

void Connection::onCancel(CancelReason r) {
    // try cancelling current
    // current_ should have been dettached,
    // but our state machine may still using it;
}

void Connection::closeOnError(const MySqlError &err) {
    finishAllConnects(err);
    finishAllWrites(err);
    finishCurrentQuery(err);
    if (s_ != CLOSED) {
        FUTURES_LOG(INFO) << "Force closing";
        mysql_close(&inst_);
        s_ = CLOSED;
    }
}

void Connection::finishCurrentQuery(const MySqlError &err) {
    if (current_) current_->setError(err);
    current_.reset();
    if (result_) mysql_free_result(result_);
    result_ = nullptr;
    err_ = 0;
    berr_ = 0;
    row_ = nullptr;
}

MySqlError Connection::getLastMySqlError() {
    if (mysql_errno(&inst_)) {
        return MySqlError(mysql_errno(&inst_), mysql_error(&inst_));
    } else {
        return MySqlError();
    }
}

std::string Connection::escapeString(const std::string &s) {
    std::string r(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(&inst_, &r[0], s.data(), s.size());
    r.resize(len);
    return r;
}

detail::ConnFuture Connection::connect(EventExecutor *ev, const Config &config) {
    return detail::ConnFuture(ev, config);
}

detail::ExecFuture Connection::exec(const std::string &q) {
    return detail::ExecFuture(shared_from_this(), WriteCommandRequest::Query, q, false);
}

detail::ExecFuture Connection::query(const std::string &q) {
    return detail::ExecFuture(shared_from_this(), WriteCommandRequest::Query, q, true);
}

detail::StmtFuture Connection::prepare(const std::string &q) {
    return detail::StmtFuture(shared_from_this(), q);
}

detail::DropExecFuture Connection::transaction() {
    return detail::DropExecFuture(shared_from_this(), "START TRANSACTION");
}

detail::DropExecFuture Connection::rollback() {
    return detail::DropExecFuture(shared_from_this(), "ROLLBACK");
}

detail::DropExecFuture Connection::commit() {
    return detail::DropExecFuture(shared_from_this(), "COMMIT");
}

detail::DropExecFuture Connection::close() {
    return detail::DropExecFuture(shared_from_this(), "", WriteCommandRequest::ConnClose);
}

}
}

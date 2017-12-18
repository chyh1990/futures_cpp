#include <futures_mysql/Transaction.h>
#include <futures_mysql/Connection.h>

namespace futures {
namespace mysql {

Transaction::Transaction(Connection *conn)
    : conn_(conn) {
    assert(conn_);
}

TxFuture Transaction::rollback() {
    assert(started_);
    started_ = false;
}

TxFuture Transaction::commit() {
    assert(started_);
    started_ = false;
}

Transaction::~Transaction() {
    assert(!started_);
}

TxFuture::TxFuture(std::shared_ptr<Connection> c, bool commit)
    : c_(c), commit_(commit) {
}

Poll<TxFuture::Item> TxFuture::poll() {
    if (!tok_)
        tok_ = c_->doCommand(WriteCommandRequest::Query,
                commit_ ? "COMMIT" : "ROLLBACK", false);
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

}
}

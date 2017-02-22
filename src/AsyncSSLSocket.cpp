#include <futures/io/AsyncSSLSocket.h>
#include <futures/core/ScopeGuard.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509.h>

namespace futures {
namespace io {

const size_t MAX_STACK_BUF_SIZE = 2048;

inline bool zero_return(int error, int rc) {
  return (error == SSL_ERROR_ZERO_RETURN || (rc == 0 && errno == 0));
}

SSLSocketChannel::SSLSocketChannel(EventExecutor *ev, SSLContext *ctx)
    : SocketChannel(ev), ssl_(nullptr) {
  ssl_ = SSL_new(ctx->getRaw());
  ssl_state_ = STATE_UNENCRYPTED;

    rio_.set<SSLSocketChannel, &SSLSocketChannel::onEvent>(this);
    wio_.set<SSLSocketChannel, &SSLSocketChannel::onEvent>(this);
}

inline std::error_code current_system_error(int e = errno) {
    return std::error_code(e, std::system_category());
}

SSLSocketChannel::~SSLSocketChannel() {
    if (ssl_)
        SSL_free(ssl_);
}

void SSLSocketChannel::onEvent(ev::io& watcher, int revent) {
    FUTURES_DLOG(INFO) << "State: " << s_ << ", " << ssl_state_ << ", event: " << revent;
    if (s_ < SocketChannel::CONNECTED) {
        SocketChannel::onEvent(watcher, revent);
        return;
    }
    if (ssl_state_ == STATE_CONNECTING) {
        handleConnect();
    } else {
        SocketChannel::onEvent(watcher, revent);
    }
}

io::intrusive_ptr<SSLSocketChannel::ConnectCompletionToken>
SSLSocketChannel::doHandshake()
{
    FUTURES_CHECK(socket_.isValid());
    io::intrusive_ptr<ConnectCompletionToken> p(new ConnectCompletionToken(this));
    if (!(s_ == CONNECTING || s_ == CONNECTED)) {
        // TODO
        FUTURES_LOG(ERROR) << "XXX " << s_;
        p->ec = std::make_error_code(std::errc::already_connected);
        p->notifyDone();
        return p;
    }
    SSL_set_fd(ssl_, socket_.fd());

    p->attach(this);
    handleConnect();
    return p;
}


void SSLSocketChannel::handleConnect() {
    int ret = SSL_connect(ssl_);
    if (ret <= 0) {
        int sslError;
        unsigned long errError;
    //    int errorCopy = errno;
        if (willBlock(ret, &sslError, &errError)) {
            ssl_state_ = STATE_CONNECTING;
            return;
        } else {
            ssl_state_ = STATE_ERROR;
            failHandshake(std::make_error_code(std::errc::connection_reset));
            failAllWrites();
            rio_.stop();
            wio_.stop();
            return;
        }
    }

    FUTURES_DLOG(INFO) << "ssl connectted";
    ssl_state_ = STATE_ESTABLISHED;
    failHandshake(std::error_code());
    handleInitialReadWrite();
}

void SSLSocketChannel::failHandshake(std::error_code ec) {
    auto &conn = getPending(IOObject::OpConnect);
    while (!conn.empty()) {
        auto f = static_cast<ConnectCompletionToken*>(&conn.front());
        f->ec = ec;
        f->notifyDone();
    }
}

bool SSLSocketChannel::willBlock(int ret, int *sslErr, unsigned long *errOut)
{
  *errOut = 0;
  int error = *sslErr = SSL_get_error(ssl_, ret);
  if (error == SSL_ERROR_WANT_READ) {
      rio_.start();
      wio_.stop();
      return true;
  } else if (error == SSL_ERROR_WANT_WRITE) {
      FUTURES_DLOG(INFO) << "SSL_ERROR_WANT_WRITE";
      rio_.stop();
      wio_.start();
      return true;
  } else {
    unsigned long lastError = *errOut = ERR_get_error();
    FUTURES_DLOG(ERROR)
        << "AsyncSSLSocket(fd=" << socket_.fd() << ", "
            << "state=" << s_ << ", "
            << "sslState=" << ssl_state_ << ", "
            << "SSL error: " << error << ", "
            << "errno: " << errno << ", "
            << "ret: " << ret << ", "
            << "read: " << BIO_number_read(SSL_get_rbio(ssl_)) << ", "
            << "written: " << BIO_number_written(SSL_get_wbio(ssl_)) << ", "
            << "func: " << ERR_func_error_string(lastError) << ", "
            << "reason: " << ERR_reason_error_string(lastError);
    return false;

  }
}

std::error_code SSLSocketChannel::interpretSSLError(int rc, int error) {
  if (error == SSL_ERROR_WANT_READ) {
    // Even though we are attempting to write data, SSL_write() may
    // need to read data if renegotiation is being performed.  We currently
    // don't support this and just fail the write.
    FUTURES_LOG(ERROR) << "AsyncSSLSocket(fd=" << socket_.fd() << ", state=" << int(s_)
               << ", sslState=" << ssl_state_
               << "): "
               << "unsupported SSL renegotiation during write";
    return std::make_error_code(std::errc::not_supported);
  } else {
    if (zero_return(error, rc)) {
      return std::error_code();
    }
    auto errError = ERR_get_error();
    FUTURES_LOG(ERROR) << "ERROR: AsyncSSLSocket(fd=" << socket_.fd() << ", state=" << int(s_)
            << ", sslState=" << ssl_state_ << ", events="
            << "SSL error: " << error << ", errno: " << errno
            << ", func: " << ERR_func_error_string(errError)
            << ", reason: " << ERR_reason_error_string(errError);
    return std::make_error_code(std::errc::broken_pipe);
  }
}


ssize_t SSLSocketChannel::performWrite(
            const iovec* vec,
            size_t count,
            size_t* countWritten,
            size_t* partialWritten,
            std::error_code &ec)
{
  if (ssl_state_ == STATE_UNENCRYPTED) {
    return SocketChannel::performWrite(vec, count, countWritten, partialWritten, ec);
  }

  if (ssl_state_ != STATE_ESTABLISHED) {
    FUTURES_LOG(ERROR) << "cannot write in: " << ssl_state_;
    ec = std::make_error_code(std::errc::not_connected);
    return 0;
  }

  char* combinedBuf{nullptr};
  SCOPE_EXIT {
    // Note, always keep this check consistent with what we do below
    if (combinedBuf != nullptr && minWriteSize_ > MAX_STACK_BUF_SIZE) {
      delete[] combinedBuf;
    }
  };

  const bool hasEOR = false;
  *countWritten = 0;
  *partialWritten = 0;
  ssize_t totalWritten = 0;
  size_t bytesStolenFromNextBuffer = 0;
  for (uint32_t i = 0; i < count; i++) {
    const iovec* v = vec + i;
    size_t offset = bytesStolenFromNextBuffer;
    bytesStolenFromNextBuffer = 0;
    size_t len = v->iov_len - offset;
    const void* buf;
    if (len == 0) {
      (*countWritten)++;
      continue;
    }
    buf = ((const char*)v->iov_base) + offset;

    ssize_t bytes;
    uint32_t buffersStolen = 0;
    if ((len < minWriteSize_) && ((i + 1) < count)) {
      // Combine this buffer with part or all of the next buffers in
      // order to avoid really small-grained calls to SSL_write().
      // Each call to SSL_write() produces a separate record in
      // the egress SSL stream, and we've found that some low-end
      // mobile clients can't handle receiving an HTTP response
      // header and the first part of the response body in two
      // separate SSL records (even if those two records are in
      // the same TCP packet).

      if (combinedBuf == nullptr) {
        if (minWriteSize_ > MAX_STACK_BUF_SIZE) {
          // Allocate the buffer on heap
          combinedBuf = new char[minWriteSize_];
        } else {
          // Allocate the buffer on stack
          combinedBuf = (char*)alloca(minWriteSize_);
        }
      }
      assert(combinedBuf != nullptr);

      memcpy(combinedBuf, buf, len);
      do {
        // INVARIANT: i + buffersStolen == complete chunks serialized
        uint32_t nextIndex = i + buffersStolen + 1;
        bytesStolenFromNextBuffer = std::min(vec[nextIndex].iov_len,
                                             minWriteSize_ - len);
        memcpy(combinedBuf + len, vec[nextIndex].iov_base,
               bytesStolenFromNextBuffer);
        len += bytesStolenFromNextBuffer;
        if (bytesStolenFromNextBuffer < vec[nextIndex].iov_len) {
          // couldn't steal the whole buffer
          break;
        } else {
          bytesStolenFromNextBuffer = 0;
          buffersStolen++;
        }
      } while ((i + buffersStolen + 1) < count && (len < minWriteSize_));
      bytes = eorAwareSSLWrite(
        ssl_, combinedBuf, len,
        (hasEOR && i + buffersStolen + 1 == count));

    } else {
      bytes = eorAwareSSLWrite(ssl_, buf, len,
                           (hasEOR && i + 1 == count));
    }

    if (bytes <= 0) {
      int error = SSL_get_error(ssl_, bytes);
      if (error == SSL_ERROR_WANT_WRITE) {
        // The caller will register for write event if not already.
        *partialWritten = offset;
        return totalWritten;
      }
      ec = interpretSSLError(bytes, error);
      if (ec) {
        return 0;
      } // else fall through to below to correctly record totalWritten
    }

    totalWritten += bytes;

    if (bytes == (ssize_t)len) {
      // The full iovec is written.
      (*countWritten) += 1 + buffersStolen;
      i += buffersStolen;
      // continue
    } else {
      bytes += offset; // adjust bytes to account for all of v
      while (bytes >= (ssize_t)v->iov_len) {
        // We combined this buf with part or all of the next one, and
        // we managed to write all of this buf but not all of the bytes
        // from the next one that we'd hoped to write.
        bytes -= v->iov_len;
        (*countWritten)++;
        v = &(vec[++i]);
      }
      *partialWritten = bytes;
      return totalWritten;
    }
  }

  return totalWritten;


}

int SSLSocketChannel::eorAwareSSLWrite(SSL *ssl, const void *buf, int n,
                                      bool eor) {
  n = SSL_write(ssl, buf, n);
  return n;
}

ssize_t SSLSocketChannel::performRead(ReaderCompletionToken *tok, std::error_code &ec)
{
  if (ssl_state_ == STATE_UNENCRYPTED) {
    return SocketChannel::performRead(tok, ec);
  }
  while (true) {
    void *buf;
    size_t bufLen = 0;
    tok->prepareBuffer(&buf, &bufLen);

    assert(buf);
    assert(bufLen > 0);

    ssize_t read_ret = realPerformRead(buf, bufLen, ec);

    // FUTURES_DLOG(INFO) << "XXXXXXXX: " << read_ret;
    if (read_ret == READ_ERROR) {
      // TODO error handling
      ec = std::make_error_code(std::errc::connection_reset);
      tok->readError(ec);
      return read_ret;
    } else if (read_ret == READ_WOULDBLOCK) {
      tok->dataReady(0);
      return read_ret;
    } else if (read_ret == READ_EOF) {
      FUTURES_DLOG(INFO) << "Socket EOF";
      tok->readEof();
      return read_ret;
    } else {
      tok->dataReady(read_ret);
      continue;
    }
  }
}

ssize_t SSLSocketChannel::realPerformRead(void *buf, size_t bufLen, std::error_code &ec) {
  ssize_t bytes = 0;
  bytes = SSL_read(ssl_, buf, bufLen);

  if (bytes <= 0) {
    int error = SSL_get_error(ssl_, bytes);
    if (error == SSL_ERROR_WANT_READ) {
      // The caller will register for read event if not already.
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return READ_WOULDBLOCK;
      } else {
        return READ_ERROR;
      }
    } else if (error == SSL_ERROR_WANT_WRITE) {
      // TODO: Even though we are attempting to read data, SSL_read() may
      // need to write data if renegotiation is being performed.  We currently
      // don't support this and just fail the read.
      FUTURES_LOG(ERROR) << "AsyncSSLSocket(fd=" << socket_.fd() << ", state=" << int(s_)
                 << ", sslState=" << ssl_state_
                 << "): unsupported SSL renegotiation during read";
      return READ_ERROR;
    } else {
      if (zero_return(error, bytes)) {
        return bytes;
      }
      long errError = ERR_get_error();
      FUTURES_DLOG(ERROR) << "AsyncSSLSocket(fd=" << socket_.fd() << ", "
              << "state=" << s_ << ", "
              << "sslState=" << ssl_state_ << ", "
              << "bytes: " << bytes << ", "
              << "error: " << error << ", "
              << "errno: " << errno << ", "
              << "func: " << ERR_func_error_string(errError) << ", "
              << "reason: " << ERR_reason_error_string(errError);
      return READ_ERROR;
    }
  } else {
    // appBytesReceived_ += bytes;
    return bytes;
  }
}

void SSLSocketChannel::printPeerCert() {
  X509* cert = SSL_get_peer_certificate(ssl_);
  if (cert) {
    X509_print_fp(stdout, cert);
    X509_free(cert);
  }
}


}
}

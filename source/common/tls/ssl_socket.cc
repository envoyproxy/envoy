#include "source/common/tls/ssl_socket.h"

#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/hex.h"
#include "source/common/http/headers.h"
#include "source/common/tls/io_handle_bio.h"
#include "source/common/tls/ssl_handshaker.h"
#include "source/common/tls/utility.h"

#include "absl/strings/str_replace.h"
#include "openssl/err.h"
#include "openssl/x509v3.h"

using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

constexpr absl::string_view NotReadyReason{"TLS error: Secret is not supplied by SDS"};

} // namespace

absl::string_view NotReadySslSocket::failureReason() const { return NotReadyReason; }

absl::StatusOr<std::unique_ptr<SslSocket>>
SslSocket::create(Envoy::Ssl::ContextSharedPtr ctx, InitialState state,
                  const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                  Ssl::HandshakerFactoryCb handshaker_factory_cb) {
  std::unique_ptr<SslSocket> socket(new SslSocket(ctx, transport_socket_options));
  auto status = socket->initialize(state, handshaker_factory_cb);
  if (status.ok()) {
    return socket;
  } else {
    return status;
  }
}

SslSocket::SslSocket(Envoy::Ssl::ContextSharedPtr ctx,
                     const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options)
    : transport_socket_options_(transport_socket_options),
      ctx_(std::dynamic_pointer_cast<ContextImpl>(ctx)) {}

absl::Status SslSocket::initialize(InitialState state,
                                   Ssl::HandshakerFactoryCb handshaker_factory_cb) {
  auto status_or_ssl = ctx_->newSsl(transport_socket_options_);
  if (!status_or_ssl.ok()) {
    return status_or_ssl.status();
  }

  info_ = std::dynamic_pointer_cast<SslHandshakerImpl>(handshaker_factory_cb(
      std::move(status_or_ssl.value()), ctx_->sslExtendedSocketInfoIndex(), this));

  if (state == InitialState::Client) {
    SSL_set_connect_state(rawSsl());
  } else {
    ASSERT(state == InitialState::Server);
    SSL_set_accept_state(rawSsl());
  }

  return absl::OkStatus();
}

void SslSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;

  // Associate this SSL connection with all the certificates (with their potentially different
  // private key methods).
  for (auto const& provider : ctx_->getPrivateKeyMethodProviders()) {
    provider->registerPrivateKeyMethod(rawSsl(), *this, callbacks_->connection().dispatcher());
  }

  // Use custom BIO that reads from/writes to IoHandle
  BIO* bio = BIO_new_io_handle(&callbacks_->ioHandle());
  SSL_set_bio(rawSsl(), bio, bio);
  SSL_set_ex_data(rawSsl(), ContextImpl::sslSocketIndex(), static_cast<void*>(callbacks_));
}

SslSocket::ReadResult SslSocket::sslReadIntoSlice(Buffer::RawSlice& slice) {
  ReadResult result;
  uint8_t* mem = static_cast<uint8_t*>(slice.mem_);
  size_t remaining = slice.len_;
  while (remaining > 0) {
    int rc = SSL_read(rawSsl(), mem, remaining);
    ENVOY_CONN_LOG(trace, "ssl read returns: {}", callbacks_->connection(), rc);
    if (rc > 0) {
      ASSERT(static_cast<size_t>(rc) <= remaining);
      mem += rc;
      remaining -= rc;
      result.bytes_read_ += rc;
    } else {
      result.error_ = absl::make_optional<int>(rc);
      break;
    }
  }

  return result;
}

Network::IoResult SslSocket::doRead(Buffer::Instance& read_buffer) {
  if (info_->state() != Ssl::SocketState::HandshakeComplete &&
      info_->state() != Ssl::SocketState::ShutdownSent) {
    PostIoAction action = doHandshake();
    if (action == PostIoAction::Close || info_->state() != Ssl::SocketState::HandshakeComplete) {
      // end_stream is false because either a hard error occurred (action == Close) or
      // the handshake isn't complete, so a half-close cannot occur yet.
      return {action, 0, false};
    }
  }

  bool keep_reading = true;
  bool end_stream = false;
  PostIoAction action = PostIoAction::KeepOpen;
  uint64_t bytes_read = 0;
  while (keep_reading) {
    uint64_t bytes_read_this_iteration = 0;
    Buffer::Reservation reservation = read_buffer.reserveForRead();
    for (uint64_t i = 0; i < reservation.numSlices(); i++) {
      auto result = sslReadIntoSlice(reservation.slices()[i]);
      bytes_read_this_iteration += result.bytes_read_;
      if (result.error_.has_value()) {
        keep_reading = false;
        int err = SSL_get_error(rawSsl(), result.error_.value());
        ENVOY_CONN_LOG(trace, "ssl error occurred while read: {}", callbacks_->connection(),
                       Utility::getErrorDescription(err));
        switch (err) {
        case SSL_ERROR_WANT_READ:
          break;
        case SSL_ERROR_ZERO_RETURN:
          // Graceful shutdown using close_notify TLS alert.
          end_stream = true;
          break;
        case SSL_ERROR_SYSCALL:
          if (result.error_.value() == 0) {
            // Non-graceful shutdown by closing the underlying socket.
            end_stream = true;
            break;
          }
          FALLTHRU;
        case SSL_ERROR_WANT_WRITE:
          // Renegotiation has started. We don't handle renegotiation so just fall through.
        default:
          drainErrorQueue();
          action = PostIoAction::Close;
          break;
        }

        break;
      }
    }

    reservation.commit(bytes_read_this_iteration);
    if (bytes_read_this_iteration > 0 && callbacks_->shouldDrainReadBuffer()) {
      callbacks_->setTransportSocketIsReadable();
      keep_reading = false;
    }

    bytes_read += bytes_read_this_iteration;
  }

  ENVOY_CONN_LOG(trace, "ssl read {} bytes", callbacks_->connection(), bytes_read);

  return {action, bytes_read, end_stream};
}

void SslSocket::onPrivateKeyMethodComplete() { resumeHandshake(); }

void SslSocket::resumeHandshake() {
  ASSERT(callbacks_ != nullptr && callbacks_->connection().dispatcher().isThreadSafe());
  ASSERT(info_->state() == Ssl::SocketState::HandshakeInProgress);

  // Resume handshake.
  PostIoAction action = doHandshake();
  if (action == PostIoAction::Close) {
    ENVOY_CONN_LOG(debug, "async handshake completion error", callbacks_->connection());
    callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite,
                                   "failed_resuming_async_handshake");
  }
}

Network::Connection& SslSocket::connection() const { return callbacks_->connection(); }

void SslSocket::onSuccess(SSL* ssl) {
  ctx_->logHandshake(ssl);
  if (callbacks_->connection().streamInfo().upstreamInfo()) {
    callbacks_->connection()
        .streamInfo()
        .upstreamInfo()
        ->upstreamTiming()
        .onUpstreamHandshakeComplete(callbacks_->connection().dispatcher().timeSource());
  } else {
    callbacks_->connection().streamInfo().downstreamTiming().onDownstreamHandshakeComplete(
        callbacks_->connection().dispatcher().timeSource());
  }
  callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
}

void SslSocket::onFailure() { drainErrorQueue(); }

PostIoAction SslSocket::doHandshake() { return info_->doHandshake(); }

void SslSocket::drainErrorQueue() {
  bool saw_error = false;
  bool saw_counted_error = false;
  while (uint64_t err = ERR_get_error()) {
    if (ERR_GET_LIB(err) == ERR_LIB_SSL) {
      if (ERR_GET_REASON(err) == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE) {
        ctx_->stats().fail_verify_no_cert_.inc();
        saw_counted_error = true;
      } else if (ERR_GET_REASON(err) == SSL_R_CERTIFICATE_VERIFY_FAILED) {
        saw_counted_error = true;
      }
    } else if (ERR_GET_LIB(err) == ERR_LIB_SYS) {
      // Any syscall errors that result in connection closure are already tracked in other
      // connection related stats. We will still retain the specific syscall failure for
      // transport failure reasons.
      saw_counted_error = true;
    }
    saw_error = true;

    if (failure_reason_.empty()) {
      failure_reason_ = "TLS_error:";
    }

    absl::StrAppend(&failure_reason_, "|", err, ":",
                    absl::NullSafeStringView(ERR_lib_error_string(err)), ":",
                    absl::NullSafeStringView(ERR_func_error_string(err)), ":",
                    absl::NullSafeStringView(ERR_reason_error_string(err)));
  }

  if (!saw_error) {
    return;
  }

  if (!failure_reason_.empty()) {
    absl::StrAppend(&failure_reason_, ":TLS_error_end");
    ENVOY_CONN_LOG(debug, "remote address:{},{}", callbacks_->connection(),
                   callbacks_->connection().connectionInfoProvider().remoteAddress()->asString(),
                   failure_reason_);
  }

  if (!saw_counted_error) {
    ctx_->stats().connection_error_.inc();
  }
}

Network::IoResult SslSocket::doWrite(Buffer::Instance& write_buffer, bool end_stream) {
  ASSERT(info_->state() != Ssl::SocketState::ShutdownSent || write_buffer.length() == 0);
  if (info_->state() != Ssl::SocketState::HandshakeComplete &&
      info_->state() != Ssl::SocketState::ShutdownSent) {
    PostIoAction action = doHandshake();
    if (action == PostIoAction::Close || info_->state() != Ssl::SocketState::HandshakeComplete) {
      return {action, 0, false};
    }
  }

  uint64_t bytes_to_write;
  if (bytes_to_retry_) {
    bytes_to_write = bytes_to_retry_;
    bytes_to_retry_ = 0;
  } else {
    bytes_to_write = std::min(write_buffer.length(), static_cast<uint64_t>(16384));
  }

  uint64_t total_bytes_written = 0;
  while (bytes_to_write > 0) {
    // TODO(mattklein123): As it relates to our fairness efforts, we might want to limit the number
    // of iterations of this loop, either by pure iterations, bytes written, etc.

    // SSL_write() requires that if a previous call returns SSL_ERROR_WANT_WRITE, we need to call
    // it again with the same parameters. This is done by tracking last write size, but not write
    // data, since linearize() will return the same undrained data anyway.
    ASSERT(bytes_to_write <= write_buffer.length());
    int rc = SSL_write(rawSsl(), write_buffer.linearize(bytes_to_write), bytes_to_write);
    ENVOY_CONN_LOG(trace, "ssl write returns: {}", callbacks_->connection(), rc);
    if (rc > 0) {
      ASSERT(rc == static_cast<int>(bytes_to_write));
      total_bytes_written += rc;
      write_buffer.drain(rc);
      bytes_to_write = std::min(write_buffer.length(), static_cast<uint64_t>(16384));
    } else {
      int err = SSL_get_error(rawSsl(), rc);
      ENVOY_CONN_LOG(trace, "ssl error occurred while write: {}", callbacks_->connection(),
                     Utility::getErrorDescription(err));
      switch (err) {
      case SSL_ERROR_WANT_WRITE:
        bytes_to_retry_ = bytes_to_write;
        break;
      case SSL_ERROR_WANT_READ:
      // Renegotiation has started. We don't handle renegotiation so just fall through.
      default:
        drainErrorQueue();
        return {PostIoAction::Close, total_bytes_written, false};
      }

      break;
    }
  }

  if (write_buffer.length() == 0 && end_stream) {
    shutdownSsl();
  }

  return {PostIoAction::KeepOpen, total_bytes_written, false};
}

void SslSocket::onConnected() { ASSERT(info_->state() == Ssl::SocketState::PreHandshake); }

Ssl::ConnectionInfoConstSharedPtr SslSocket::ssl() const { return info_; }

void SslSocket::shutdownSsl() {
  ASSERT(info_->state() != Ssl::SocketState::PreHandshake);
  if (info_->state() != Ssl::SocketState::ShutdownSent &&
      callbacks_->connection().state() != Network::Connection::State::Closed) {
    int rc = SSL_shutdown(rawSsl());
    if constexpr (Event::PlatformDefaultTriggerType == Event::FileTriggerType::EmulatedEdge) {
      // Windows operate under `EmulatedEdge`. These are level events that are artificially
      // made to behave like edge events. And if the rc is 0 then in that case we want read
      // activation resumption. This code is protected with an `constexpr` if, to minimize the tax
      // on POSIX systems that operate in Edge events.
      if (rc == 0) {
        // See https://www.openssl.org/docs/manmaster/man3/SSL_shutdown.html
        // if return value is 0,  Call SSL_read() to do a bidirectional shutdown.
        callbacks_->setTransportSocketIsReadable();
      }
    }
    ENVOY_CONN_LOG(debug, "SSL shutdown: rc={}", callbacks_->connection(), rc);
    drainErrorQueue();
    info_->setState(Ssl::SocketState::ShutdownSent);
  }
}

void SslSocket::shutdownBasic() {
  if (info_->state() != Ssl::SocketState::ShutdownSent &&
      callbacks_->connection().state() != Network::Connection::State::Closed) {
    callbacks_->ioHandle().shutdown(ENVOY_SHUT_WR);
    drainErrorQueue();
    info_->setState(Ssl::SocketState::ShutdownSent);
  }
}

void SslSocket::closeSocket(Network::ConnectionEvent) {
  // Unregister the SSL connection object from private key method providers.
  for (auto const& provider : ctx_->getPrivateKeyMethodProviders()) {
    provider->unregisterPrivateKeyMethod(rawSsl());
  }

  // Attempt to send a shutdown before closing the socket. It's possible this won't go out if
  // there is no room on the socket. We can extend the state machine to handle this at some point
  // if needed.
  if (info_->state() == Ssl::SocketState::HandshakeInProgress ||
      info_->state() == Ssl::SocketState::HandshakeComplete) {
    shutdownSsl();
  } else {
    // We're not in a state to do the full SSL shutdown so perform a basic shutdown to flush any
    // outstanding alerts
    shutdownBasic();
  }
}

std::string SslSocket::protocol() const { return ssl()->alpn(); }

absl::string_view SslSocket::failureReason() const { return failure_reason_; }

void SslSocket::onAsynchronousCertValidationComplete() {
  ENVOY_CONN_LOG(debug, "Async cert validation completed", callbacks_->connection());
  if (info_->state() == Ssl::SocketState::HandshakeInProgress) {
    resumeHandshake();
  }
}

void SslSocket::onAsynchronousCertificateSelectionComplete() {
  ENVOY_CONN_LOG(debug, "Async cert selection completed", callbacks_->connection());
  if (info_->state() != Ssl::SocketState::HandshakeInProgress) {
    IS_ENVOY_BUG(fmt::format("unexpected handshake state: {}", static_cast<int>(info_->state())));
    return;
  }
  resumeHandshake();
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

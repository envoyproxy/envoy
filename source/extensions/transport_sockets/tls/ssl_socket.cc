#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/hex.h"
#include "common/http/headers.h"
#include "common/runtime/runtime_features.h"

#include "extensions/transport_sockets/tls/io_handle_bio.h"
#include "extensions/transport_sockets/tls/ssl_handshaker.h"
#include "extensions/transport_sockets/tls/utility.h"

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

// This SslSocket will be used when SSL secret is not fetched from SDS server.
class NotReadySslSocket : public Network::TransportSocket {
public:
  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks&) override {}
  std::string protocol() const override { return EMPTY_STRING; }
  absl::string_view failureReason() const override { return NotReadyReason; }
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent) override {}
  Network::IoResult doRead(Buffer::Instance&) override { return {PostIoAction::Close, 0, false}; }
  Network::IoResult doWrite(Buffer::Instance&, bool) override {
    return {PostIoAction::Close, 0, false};
  }
  void onConnected() override {}
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
};
} // namespace

SslSocket::SslSocket(Envoy::Ssl::ContextSharedPtr ctx, InitialState state,
                     const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                     Ssl::HandshakerFactoryCb handshaker_factory_cb)
    : transport_socket_options_(transport_socket_options),
      ctx_(std::dynamic_pointer_cast<ContextImpl>(ctx)),
      info_(std::dynamic_pointer_cast<SslHandshakerImpl>(
          handshaker_factory_cb(ctx_->newSsl(transport_socket_options_.get()),
                                ctx_->sslExtendedSocketInfoIndex(), this))) {
  if (state == InitialState::Client) {
    SSL_set_connect_state(rawSsl());
  } else {
    ASSERT(state == InitialState::Server);
    SSL_set_accept_state(rawSsl());
  }
}

void SslSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;

  // Associate this SSL connection with all the certificates (with their potentially different
  // private key methods).
  for (auto const& provider : ctx_->getPrivateKeyMethodProviders()) {
    provider->registerPrivateKeyMethod(rawSsl(), *this, callbacks_->connection().dispatcher());
  }

  BIO* bio;
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_use_io_handle_bio")) {
    // Use custom BIO that reads from/writes to IoHandle
    bio = BIO_new_io_handle(&callbacks_->ioHandle());
  } else {
    // TODO(fcoras): remove once the io_handle_bio proves to be stable
    bio = BIO_new_socket(callbacks_->ioHandle().fdDoNotUse(), 0);
  }
  SSL_set_bio(rawSsl(), bio, bio);
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
      result.commit_slice_ = true;
    } else {
      result.error_ = absl::make_optional<int>(rc);
      break;
    }
  }

  if (result.commit_slice_) {
    slice.len_ -= remaining;
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
    // We use 2 slices here so that we can use the remainder of an existing buffer chain element
    // if there is extra space. 16K read is arbitrary and can be tuned later.
    Buffer::RawSlice slices[2];
    uint64_t slices_to_commit = 0;
    uint64_t num_slices = read_buffer.reserve(16384, slices, 2);
    for (uint64_t i = 0; i < num_slices; i++) {
      auto result = sslReadIntoSlice(slices[i]);
      if (result.commit_slice_) {
        slices_to_commit++;
        bytes_read += slices[i].len_;
      }
      if (result.error_.has_value()) {
        keep_reading = false;
        int err = SSL_get_error(rawSsl(), result.error_.value());
        switch (err) {
        case SSL_ERROR_WANT_READ:
          break;
        case SSL_ERROR_ZERO_RETURN:
          end_stream = true;
          break;
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

    if (slices_to_commit > 0) {
      read_buffer.commit(slices, slices_to_commit);
      if (callbacks_->shouldDrainReadBuffer()) {
        callbacks_->setReadBufferReady();
        keep_reading = false;
      }
    }
  }

  ENVOY_CONN_LOG(trace, "ssl read {} bytes", callbacks_->connection(), bytes_read);

  return {action, bytes_read, end_stream};
}

void SslSocket::onPrivateKeyMethodComplete() {
  ASSERT(isThreadSafe());
  ASSERT(info_->state() == Ssl::SocketState::HandshakeInProgress);

  // Resume handshake.
  PostIoAction action = doHandshake();
  if (action == PostIoAction::Close) {
    ENVOY_CONN_LOG(debug, "async handshake completion error", callbacks_->connection());
    callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

Network::Connection& SslSocket::connection() const { return callbacks_->connection(); }

void SslSocket::onSuccess(SSL* ssl) {
  ctx_->logHandshake(ssl);
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
    }
    saw_error = true;

    if (failure_reason_.empty()) {
      failure_reason_ = "TLS error:";
    }
    failure_reason_.append(absl::StrCat(" ", err, ":", ERR_lib_error_string(err), ":",
                                        ERR_func_error_string(err), ":",
                                        ERR_reason_error_string(err)));
  }
  ENVOY_CONN_LOG(debug, "{}", callbacks_->connection(), failure_reason_);
  if (saw_error && !saw_counted_error) {
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
    ENVOY_CONN_LOG(debug, "SSL shutdown: rc={}", callbacks_->connection(), rc);
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
  }
}

std::string SslSocket::protocol() const {
  const unsigned char* proto;
  unsigned int proto_len;
  SSL_get0_alpn_selected(rawSsl(), &proto, &proto_len);
  return std::string(reinterpret_cast<const char*>(proto), proto_len);
}

absl::string_view SslSocket::failureReason() const { return failure_reason_; }

namespace {
SslSocketFactoryStats generateStats(const std::string& prefix, Stats::Scope& store) {
  return {
      ALL_SSL_SOCKET_FACTORY_STATS(POOL_COUNTER_PREFIX(store, prefix + "_ssl_socket_factory."))};
}
} // namespace

ClientSslSocketFactory::ClientSslSocketFactory(Envoy::Ssl::ClientContextConfigPtr config,
                                               Envoy::Ssl::ContextManager& manager,
                                               Stats::Scope& stats_scope)
    : manager_(manager), stats_scope_(stats_scope), stats_(generateStats("client", stats_scope)),
      config_(std::move(config)),
      ssl_ctx_(manager_.createSslClientContext(stats_scope_, *config_, nullptr)) {
  config_->setSecretUpdateCallback([this]() { onAddOrUpdateSecret(); });
}

Network::TransportSocketPtr ClientSslSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsSharedPtr transport_socket_options) const {
  // onAddOrUpdateSecret() could be invoked in the middle of checking the existence of ssl_ctx and
  // creating SslSocket using ssl_ctx. Capture ssl_ctx_ into a local variable so that we check and
  // use the same ssl_ctx to create SslSocket.
  Envoy::Ssl::ClientContextSharedPtr ssl_ctx;
  {
    absl::ReaderMutexLock l(&ssl_ctx_mu_);
    ssl_ctx = ssl_ctx_;
  }
  if (ssl_ctx) {
    return std::make_unique<SslSocket>(std::move(ssl_ctx), InitialState::Client,
                                       transport_socket_options, config_->createHandshaker());
  } else {
    ENVOY_LOG(debug, "Create NotReadySslSocket");
    stats_.upstream_context_secrets_not_ready_.inc();
    return std::make_unique<NotReadySslSocket>();
  }
}

bool ClientSslSocketFactory::implementsSecureTransport() const { return true; }

void ClientSslSocketFactory::onAddOrUpdateSecret() {
  ENVOY_LOG(debug, "Secret is updated.");
  {
    absl::WriterMutexLock l(&ssl_ctx_mu_);
    ssl_ctx_ = manager_.createSslClientContext(stats_scope_, *config_, ssl_ctx_);
  }
  stats_.ssl_context_update_by_sds_.inc();
}

bool ClientSslSocketFactory::isReady() const { return config_->isSecretReady(); }

ServerSslSocketFactory::ServerSslSocketFactory(Envoy::Ssl::ServerContextConfigPtr config,
                                               Envoy::Ssl::ContextManager& manager,
                                               Stats::Scope& stats_scope,
                                               const std::vector<std::string>& server_names)
    : manager_(manager), stats_scope_(stats_scope), stats_(generateStats("server", stats_scope)),
      config_(std::move(config)), server_names_(server_names),
      ssl_ctx_(manager_.createSslServerContext(stats_scope_, *config_, server_names_, nullptr)) {
  config_->setSecretUpdateCallback([this]() { onAddOrUpdateSecret(); });
}

Network::TransportSocketPtr
ServerSslSocketFactory::createTransportSocket(Network::TransportSocketOptionsSharedPtr) const {
  // onAddOrUpdateSecret() could be invoked in the middle of checking the existence of ssl_ctx and
  // creating SslSocket using ssl_ctx. Capture ssl_ctx_ into a local variable so that we check and
  // use the same ssl_ctx to create SslSocket.
  Envoy::Ssl::ServerContextSharedPtr ssl_ctx;
  {
    absl::ReaderMutexLock l(&ssl_ctx_mu_);
    ssl_ctx = ssl_ctx_;
  }
  if (ssl_ctx) {
    return std::make_unique<SslSocket>(std::move(ssl_ctx), InitialState::Server, nullptr,
                                       config_->createHandshaker());
  } else {
    ENVOY_LOG(debug, "Create NotReadySslSocket");
    stats_.downstream_context_secrets_not_ready_.inc();
    return std::make_unique<NotReadySslSocket>();
  }
}

bool ServerSslSocketFactory::implementsSecureTransport() const { return true; }

void ServerSslSocketFactory::onAddOrUpdateSecret() {
  ENVOY_LOG(debug, "Secret is updated.");
  {
    absl::WriterMutexLock l(&ssl_ctx_mu_);
    ssl_ctx_ = manager_.createSslServerContext(stats_scope_, *config_, server_names_, ssl_ctx_);
  }
  stats_.ssl_context_update_by_sds_.inc();
}

bool ServerSslSocketFactory::isReady() const { return true; }

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

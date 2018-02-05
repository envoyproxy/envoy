#include "common/ssl/ssl_socket.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/hex.h"
#include "common/http/headers.h"

#include "absl/strings/str_replace.h"
#include "openssl/err.h"
#include "openssl/x509v3.h"

using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Ssl {

SslSocket::SslSocket(Context& ctx, InitialState state)
    : ctx_(dynamic_cast<Ssl::ContextImpl&>(ctx)), ssl_(ctx_.newSsl()) {
  SSL_set_mode(ssl_.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
  if (state == InitialState::Client) {
    SSL_set_connect_state(ssl_.get());
  } else {
    ASSERT(state == InitialState::Server);
    SSL_set_accept_state(ssl_.get());
  }
}

void SslSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;

  BIO* bio = BIO_new_socket(callbacks_->fd(), 0);
  SSL_set_bio(ssl_.get(), bio, bio);
}

Network::IoResult SslSocket::doRead(Buffer::Instance& read_buffer) {
  if (!handshake_complete_) {
    PostIoAction action = doHandshake();
    if (action == PostIoAction::Close || !handshake_complete_) {
      return {action, 0};
    }
  }

  bool keep_reading = true;
  PostIoAction action = PostIoAction::KeepOpen;
  uint64_t bytes_read = 0;
  while (keep_reading) {
    // We use 2 slices here so that we can use the remainder of an existing buffer chain element
    // if there is extra space. 16K read is arbitrary and can be tuned later.
    Buffer::RawSlice slices[2];
    uint64_t slices_to_commit = 0;
    uint64_t num_slices = read_buffer.reserve(16384, slices, 2);
    for (uint64_t i = 0; i < num_slices; i++) {
      int rc = SSL_read(ssl_.get(), slices[i].mem_, slices[i].len_);
      ENVOY_CONN_LOG(trace, "ssl read returns: {}", callbacks_->connection(), rc);
      if (rc > 0) {
        slices[i].len_ = rc;
        slices_to_commit++;
        bytes_read += rc;
      } else {
        keep_reading = false;
        int err = SSL_get_error(ssl_.get(), rc);
        switch (err) {
        case SSL_ERROR_WANT_READ:
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

  return {action, bytes_read};
}

PostIoAction SslSocket::doHandshake() {
  ASSERT(!handshake_complete_);
  int rc = SSL_do_handshake(ssl_.get());
  if (rc == 1) {
    ENVOY_CONN_LOG(debug, "handshake complete", callbacks_->connection());
    handshake_complete_ = true;
    ctx_.logHandshake(ssl_.get());
    callbacks_->raiseEvent(Network::ConnectionEvent::Connected);

    // It's possible that we closed during the handshake callback.
    return callbacks_->connection().state() == Network::Connection::State::Open
               ? PostIoAction::KeepOpen
               : PostIoAction::Close;
  } else {
    int err = SSL_get_error(ssl_.get(), rc);
    ENVOY_CONN_LOG(debug, "handshake error: {}", callbacks_->connection(), err);
    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return PostIoAction::KeepOpen;
    default:
      drainErrorQueue();
      return PostIoAction::Close;
    }
  }
}

void SslSocket::drainErrorQueue() {
  bool saw_error = false;
  bool saw_counted_error = false;
  while (uint64_t err = ERR_get_error()) {
    if (ERR_GET_LIB(err) == ERR_LIB_SSL) {
      if (ERR_GET_REASON(err) == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE) {
        ctx_.stats().fail_verify_no_cert_.inc();
        saw_counted_error = true;
      } else if (ERR_GET_REASON(err) == SSL_R_CERTIFICATE_VERIFY_FAILED) {
        saw_counted_error = true;
      }
    }
    saw_error = true;

    ENVOY_CONN_LOG(debug, "SSL error: {}:{}:{}:{}", callbacks_->connection(), err,
                   ERR_lib_error_string(err), ERR_func_error_string(err),
                   ERR_reason_error_string(err));
    UNREFERENCED_PARAMETER(err);
  }
  if (saw_error && !saw_counted_error) {
    ctx_.stats().connection_error_.inc();
  }
}

Network::IoResult SslSocket::doWrite(Buffer::Instance& write_buffer) {
  if (!handshake_complete_) {
    PostIoAction action = doHandshake();
    if (action == PostIoAction::Close || !handshake_complete_) {
      return {action, 0};
    }
  }

  uint64_t original_buffer_length = write_buffer.length();
  uint64_t total_bytes_written = 0;
  bool keep_writing = true;
  while ((original_buffer_length != total_bytes_written) && keep_writing) {
    // Protect against stack overflow if the buffer has a very large buffer chain.
    // TODO(mattklein123): See the comment on getRawSlices() for why we have to also check
    // original_buffer_length != total_bytes_written during loop iteration.
    // TODO(mattklein123): As it relates to our fairness efforts, we might want to limit the number
    // of iterations of this loop, either by pure iterations, bytes written, etc.
    const uint64_t MAX_SLICES = 32;
    Buffer::RawSlice slices[MAX_SLICES];
    uint64_t num_slices = write_buffer.getRawSlices(slices, MAX_SLICES);

    uint64_t inner_bytes_written = 0;
    for (uint64_t i = 0; (i < num_slices) && (original_buffer_length != total_bytes_written); i++) {
      // SSL_write() requires that if a previous call returns SSL_ERROR_WANT_WRITE, we need to call
      // it again with the same parameters. Most implementations keep track of the last write size.
      // In our case we don't need to do that because: a) SSL_write() will not write partial
      // buffers. b) We only move() into the write buffer, which means that it's impossible for a
      // particular chain to increase in size. So as long as we start writing where we left off we
      // are guaranteed to call SSL_write() with the same parameters.
      int rc = SSL_write(ssl_.get(), slices[i].mem_, slices[i].len_);
      ENVOY_CONN_LOG(trace, "ssl write returns: {}", callbacks_->connection(), rc);
      if (rc > 0) {
        inner_bytes_written += rc;
        total_bytes_written += rc;
      } else {
        int err = SSL_get_error(ssl_.get(), rc);
        switch (err) {
        case SSL_ERROR_WANT_WRITE:
          keep_writing = false;
          break;
        case SSL_ERROR_WANT_READ:
        // Renegotiation has started. We don't handle renegotiation so just fall through.
        default:
          drainErrorQueue();
          return {PostIoAction::Close, total_bytes_written};
        }

        break;
      }
    }

    // Draining must be done within the inner loop, otherwise we will keep getting the same slices
    // at the beginning of the buffer.
    if (inner_bytes_written > 0) {
      write_buffer.drain(inner_bytes_written);
    }
  }

  return {PostIoAction::KeepOpen, total_bytes_written};
}

void SslSocket::onConnected() { ASSERT(!handshake_complete_); }

bool SslSocket::peerCertificatePresented() const {
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl_.get()));
  return cert != nullptr;
}

std::string SslSocket::uriSanLocalCertificate() {
  // The cert object is not owned.
  X509* cert = SSL_get_certificate(ssl_.get());
  if (!cert) {
    return "";
  }
  return getUriSanFromCertificate(cert);
}

const std::string& SslSocket::sha256PeerCertificateDigest() const {
  if (!cached_sha_256_peer_certificate_digest_.empty()) {
    return cached_sha_256_peer_certificate_digest_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl_.get()));
  if (!cert) {
    ASSERT(cached_sha_256_peer_certificate_digest_.empty());
    return cached_sha_256_peer_certificate_digest_;
  }

  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert.get(), EVP_sha256(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size());
  cached_sha_256_peer_certificate_digest_ = Hex::encode(computed_hash);
  return cached_sha_256_peer_certificate_digest_;
}

const std::string& SslSocket::urlEncodedPemEncodedPeerCertificate() const {
  if (!cached_url_encoded_pem_encoded_peer_certificate_.empty()) {
    return cached_url_encoded_pem_encoded_peer_certificate_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl_.get()));
  if (!cert) {
    ASSERT(cached_url_encoded_pem_encoded_peer_certificate_.empty());
    return cached_url_encoded_pem_encoded_peer_certificate_;
  }

  bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
  RELEASE_ASSERT(buf != nullptr);
  RELEASE_ASSERT(PEM_write_bio_X509(buf.get(), cert.get()) == 1);
  const uint8_t* output;
  size_t length;
  RELEASE_ASSERT(BIO_mem_contents(buf.get(), &output, &length) == 1);
  absl::string_view pem(reinterpret_cast<const char*>(output), length);
  cached_url_encoded_pem_encoded_peer_certificate_ = absl::StrReplaceAll(
      pem, {{"\n", "%0A"}, {" ", "%20"}, {"+", "%2B"}, {"/", "%2F"}, {"=", "%3D"}});
  return cached_url_encoded_pem_encoded_peer_certificate_;
}

std::string SslSocket::uriSanPeerCertificate() {
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl_.get()));
  if (!cert) {
    return "";
  }
  return getUriSanFromCertificate(cert.get());
}

std::string SslSocket::getUriSanFromCertificate(X509* cert) {
  STACK_OF(GENERAL_NAME)* altnames = static_cast<STACK_OF(GENERAL_NAME)*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

  if (altnames == nullptr) {
    return "";
  }

  std::string result;
  int n = sk_GENERAL_NAME_num(altnames);
  if (n > 0) {
    // Only take the first item in altnames since we only set one uri in cert.
    GENERAL_NAME* altname = sk_GENERAL_NAME_value(altnames, 0);
    switch (altname->type) {
    case GEN_URI:
      result.append(
          reinterpret_cast<const char*>(ASN1_STRING_data(altname->d.uniformResourceIdentifier)));
      break;
    default:
      // Default to empty;
      break;
    }
  }

  sk_GENERAL_NAME_pop_free(altnames, GENERAL_NAME_free);
  return result;
}

void SslSocket::closeSocket(Network::ConnectionEvent) {
  if (handshake_complete_ &&
      callbacks_->connection().state() != Network::Connection::State::Closed) {
    // Attempt to send a shutdown before closing the socket. It's possible this won't go out if
    // there is no room on the socket. We can extend the state machine to handle this at some point
    // if needed.
    int rc = SSL_shutdown(ssl_.get());
    ENVOY_CONN_LOG(debug, "SSL shutdown: rc={}", callbacks_->connection(), rc);
    UNREFERENCED_PARAMETER(rc);
    drainErrorQueue();
  }
}

std::string SslSocket::protocol() const {
  const unsigned char* proto;
  unsigned int proto_len;
  SSL_get0_alpn_selected(ssl_.get(), &proto, &proto_len);
  return std::string(reinterpret_cast<const char*>(proto), proto_len);
}

std::string SslSocket::getSubjectFromCertificate(X509* cert) const {
  bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
  RELEASE_ASSERT(buf != nullptr);

  // flags=XN_FLAG_RFC2253 is the documented parameter for single-line output in RFC 2253 format.
  // Example from the RFC:
  //   * Single value per Relative Distinguished Name (RDN): CN=Steve Kille,O=Isode Limited,C=GB
  //   * Multivalue output in first RDN: OU=Sales+CN=J. Smith,O=Widget Inc.,C=US
  //   * Quoted comma in Organization: CN=L. Eagle,O=Sue\, Grabbit and Runn,C=GB
  X509_NAME_print_ex(buf.get(), X509_get_subject_name(cert), 0 /* indent */, XN_FLAG_RFC2253);

  const uint8_t* data;
  size_t data_len;
  int rc = BIO_mem_contents(buf.get(), &data, &data_len);
  ASSERT(rc == 1);
  UNREFERENCED_PARAMETER(rc);
  return std::string(reinterpret_cast<const char*>(data), data_len);
}

std::string SslSocket::subjectPeerCertificate() const {
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl_.get()));
  if (!cert) {
    return "";
  }
  return getSubjectFromCertificate(cert.get());
}

std::string SslSocket::subjectLocalCertificate() const {
  X509* cert = SSL_get_certificate(ssl_.get());
  if (!cert) {
    return "";
  }
  return getSubjectFromCertificate(cert);
}

ClientSslSocketFactory::ClientSslSocketFactory(const ClientContextConfig& config,
                                               Ssl::ContextManager& manager,
                                               Stats::Scope& stats_scope)
    : ssl_ctx_(manager.createSslClientContext(stats_scope, config)) {}

Network::TransportSocketPtr ClientSslSocketFactory::createTransportSocket() const {
  return std::make_unique<Ssl::SslSocket>(*ssl_ctx_, Ssl::InitialState::Client);
}

bool ClientSslSocketFactory::implementsSecureTransport() const { return true; }

ServerSslSocketFactory::ServerSslSocketFactory(const ServerContextConfig& config,
                                               const std::string& listener_name,
                                               const std::vector<std::string>& server_names,
                                               bool skip_context_update,
                                               Ssl::ContextManager& manager,
                                               Stats::Scope& stats_scope)
    : ssl_ctx_(manager.createSslServerContext(listener_name, server_names, stats_scope, config,
                                              skip_context_update)) {}

Network::TransportSocketPtr ServerSslSocketFactory::createTransportSocket() const {
  return std::make_unique<Ssl::SslSocket>(*ssl_ctx_, Ssl::InitialState::Server);
}

bool ServerSslSocketFactory::implementsSecureTransport() const { return true; }

} // namespace Ssl
} // namespace Envoy

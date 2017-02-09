#include "connection_impl.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/hex.h"
#include "common/network/utility.h"

#include "openssl/err.h"

namespace Ssl {

ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd,
                               Network::Address::InstancePtr remote_address,
                               Network::Address::InstancePtr local_address, Context& ctx,
                               InitialState state)
    : Network::ConnectionImpl(dispatcher, fd, remote_address, local_address),
      ctx_(dynamic_cast<Ssl::ContextImpl&>(ctx)), ssl_(ctx_.newSsl()) {
  BIO* bio = BIO_new_socket(fd, 0);
  SSL_set_bio(ssl_.get(), bio, bio);

  SSL_set_mode(ssl_.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
  if (state == InitialState::Client) {
    SSL_set_connect_state(ssl_.get());
  } else {
    ASSERT(state == InitialState::Server);
    SSL_set_accept_state(ssl_.get());
  }
}

ConnectionImpl::~ConnectionImpl() {
  // Filters may care about whether this connection is an SSL connection or not in their
  // destructors for stat reasons. We destroy the filters here vs. the base class destructors
  // to make sure they have the chance to still inspect SSL specific data via virtual functions.
  filter_manager_.destroyFilters();
}

Network::ConnectionImpl::IoResult ConnectionImpl::doReadFromSocket() {
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
    uint64_t num_slices = read_buffer_.reserve(16384, slices, 2);
    for (uint64_t i = 0; i < num_slices; i++) {
      int rc = SSL_read(ssl_.get(), slices[i].mem_, slices[i].len_);
      conn_log_trace("ssl read returns: {}", *this, rc);
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
      read_buffer_.commit(slices, slices_to_commit);
    }
  }

  return {action, bytes_read};
}

Network::ConnectionImpl::PostIoAction ConnectionImpl::doHandshake() {
  ASSERT(!handshake_complete_);
  int rc = SSL_do_handshake(ssl_.get());
  if (rc == 1) {
    conn_log_debug("handshake complete", *this);
    if (!ctx_.verifyPeer(ssl_.get())) {
      conn_log_debug("SSL peer verification failed", *this);
      return PostIoAction::Close;
    }

    handshake_complete_ = true;
    raiseEvents(Network::ConnectionEvent::Connected);

    // It's possible that we closed during the handshake callback.
    return state() == State::Open ? PostIoAction::KeepOpen : PostIoAction::Close;
  } else {
    int err = SSL_get_error(ssl_.get(), rc);
    conn_log_debug("handshake error: {}", *this, err);
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

void ConnectionImpl::drainErrorQueue() {
  while (uint64_t err = ERR_get_error()) {
    conn_log_debug("SSL error: {}:{}:{}:{}", *this, err, ERR_lib_error_string(err),
                   ERR_func_error_string(err), ERR_reason_error_string(err));
    UNREFERENCED_PARAMETER(err);
  }
}

Network::ConnectionImpl::IoResult ConnectionImpl::doWriteToSocket() {
  if (!handshake_complete_) {
    PostIoAction action = doHandshake();
    if (action == PostIoAction::Close || !handshake_complete_) {
      return {action, 0};
    }
  }

  if (write_buffer_.length() == 0) {
    return {PostIoAction::KeepOpen, 0};
  }

  uint64_t num_slices = write_buffer_.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  write_buffer_.getRawSlices(slices, num_slices);

  uint64_t bytes_written = 0;
  for (uint64_t i = 0; i < num_slices; i++) {
    // SSL_write() requires that if a previous call returns SSL_ERROR_WANT_WRITE, we need to call
    // it again with the same parameters. Most implementations keep track of the last write size.
    // In our case we don't need to do that because: a) SSL_write() will not write partial buffers.
    // b) We only move() into the write buffer, which means that it's impossible for a particular
    // chain to increase in size. So as long as we start writing where we left off we are guaranteed
    // to call SSL_write() with the same parameters.
    int rc = SSL_write(ssl_.get(), slices[i].mem_, slices[i].len_);
    conn_log_trace("ssl write returns: {}", *this, rc);
    if (rc > 0) {
      bytes_written += rc;
    } else {
      int err = SSL_get_error(ssl_.get(), rc);
      switch (err) {
      case SSL_ERROR_WANT_WRITE:
        break;
      case SSL_ERROR_WANT_READ:
      // Renegotiation has started. We don't handle renegotiation so just fall through.
      default:
        drainErrorQueue();
        return {PostIoAction::Close, bytes_written};
      }

      break;
    }
  }

  if (bytes_written > 0) {
    write_buffer_.drain(bytes_written);
  }

  return {PostIoAction::KeepOpen, bytes_written};
}

void ConnectionImpl::onConnected() { ASSERT(!handshake_complete_); }

std::string ConnectionImpl::sha256PeerCertificateDigest() {
  X509Ptr cert = X509Ptr(SSL_get_peer_certificate(ssl_.get()));
  if (!cert) {
    return "";
  }

  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert.get(), EVP_sha256(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size());
  return Hex::encode(computed_hash);
}

ClientConnectionImpl::ClientConnectionImpl(Event::DispatcherImpl& dispatcher, Context& ctx,
                                           Network::Address::InstancePtr address)
    : ConnectionImpl(dispatcher, address->socket(Network::Address::SocketType::Stream), address,
                     null_local_address_, ctx, InitialState::Client) {}

void ClientConnectionImpl::connect() { doConnect(); }

void ConnectionImpl::closeSocket(uint32_t close_type) {
  if (handshake_complete_ && state() != State::Closed) {
    // Attempt to send a shutdown before closing the socket. It's possible this won't go out if
    // there is no room on the socket. We can extend the state machine to handle this at some point
    // if needed.
    int rc = SSL_shutdown(ssl_.get());
    conn_log_debug("SSL shutdown: rc={}", *this, rc);
    UNREFERENCED_PARAMETER(rc);
    drainErrorQueue();
  }

  Network::ConnectionImpl::closeSocket(close_type);
}

std::string ConnectionImpl::nextProtocol() {
  const unsigned char* proto;
  unsigned int proto_len;
  SSL_get0_alpn_selected(ssl_.get(), &proto, &proto_len);
  return std::string(reinterpret_cast<const char*>(proto), proto_len);
}

} // Ssl

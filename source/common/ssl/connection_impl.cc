#include "connection_impl.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/hex.h"
#include "common/network/utility.h"

#include "event2/bufferevent_ssl.h"
#include "openssl/err.h"

namespace Ssl {

ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher,
                               Event::Libevent::BufferEventPtr&& bev,
                               const std::string& remote_address, ContextImpl& ctx)
    : Network::ConnectionImpl(dispatcher, std::move(bev), remote_address), ctx_(ctx) {}

ConnectionImpl::~ConnectionImpl() {
  // Filters may care about whether this connection is an SSL connection or not in their
  // destructors for stat reasons. We destroy the filters here vs. the base class destructors
  // to make sure they have the chance to still inspect SSL specific data via virtual functions.
  filter_manager_.destroyFilters();
}

void ConnectionImpl::onEvent(short events) {
  if (events & BEV_EVENT_ERROR) {
    long error;
    do {
      error = bufferevent_get_openssl_error(bev_.get());
      if (error != 0) {
        ctx_.stats().connection_error_.inc();
        conn_log_debug("SSL error: {}:{}:{}", *this, ERR_lib_error_string(error),
                       ERR_func_error_string(error), ERR_reason_error_string(error));
      }
    } while (error != 0);
  }

  if (events & BEV_EVENT_CONNECTED) {
    // Handshake complete
    SSL* ssl = bufferevent_openssl_get_ssl(bev_.get());
    if (!ctx_.verifyPeer(ssl)) {
      conn_log_debug("SSL peer verification failed", *this);
      close(Network::ConnectionCloseType::NoFlush);
      return;
    }

    handshake_complete_ = true;
  }

  Network::ConnectionImpl::onEvent(events);
}

std::string ConnectionImpl::sha256PeerCertificateDigest() {
  X509Ptr cert = X509Ptr(SSL_get_peer_certificate(bufferevent_openssl_get_ssl(bev_.get())));
  if (!cert) {
    return "";
  }

  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert.get(), EVP_sha256(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size());
  return Hex::encode(computed_hash);
}

ClientConnectionImpl::ClientConnectionImpl(Event::DispatcherImpl& dispatcher,
                                           Event::Libevent::BufferEventPtr&& bev, ContextImpl& ctx,
                                           const std::string& url)
    : ConnectionImpl(dispatcher, std::move(bev), url, ctx) {}

void ClientConnectionImpl::connect() {
  Network::AddrInfoPtr addr_info =
      Network::Utility::resolveTCP(Network::Utility::hostFromUrl(remote_address_),
                                   Network::Utility::portFromUrl(remote_address_));
  bufferevent_socket_connect(bev_.get(), addr_info->ai_addr, addr_info->ai_addrlen);
}

void ConnectionImpl::closeBev() {
  if (handshake_complete_) {
    // SSL_RECEIVED_SHUTDOWN tells SSL_shutdown to act as if we had already received a close notify
    // from the other end.  SSL_shutdown will then send the final close notify in reply.  The other
    // end will receive the close notify and send theirs.  By this time, we will have already closed
    // the socket and the other end's real close notify will never be received.  In effect, both
    // sides will think that they have completed a clean shutdown and keep their sessions valid.
    // This strategy will fail if the socket is not ready for writing, in which case this hack will
    // lead to an unclean shutdown and lost session on the other end.
    SSL* ssl = bufferevent_openssl_get_ssl(bev_.get());
    SSL_set_shutdown(ssl, SSL_RECEIVED_SHUTDOWN);
    SSL_shutdown(ssl);

    // These calls can now alter the error stack. Make sure we clear it so that libevent does not
    // accidently pick an error up.
    ERR_clear_error();
  }

  Network::ConnectionImpl::closeBev();
}

std::string ConnectionImpl::nextProtocol() {
  const unsigned char* proto;
  unsigned int proto_len;
  SSL_get0_alpn_selected(bufferevent_openssl_get_ssl(bev_.get()), &proto, &proto_len);
  return std::string(reinterpret_cast<const char*>(proto), proto_len);
}

} // Ssl

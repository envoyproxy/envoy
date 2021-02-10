#pragma once

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "common/common/matchers.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/transport_sockets/tls/stats.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class CertValidator {
public:
  virtual ~CertValidator() = default;

  /**
   * Called to add the client validation context information to a given ssl context
   *
   * @param context the store context
   * @param require_client_cert whether or not client cert is required
   */
  virtual void addClientValidationContext(SSL_CTX* context, bool require_client_cert) PURE;

  /**
   * Called by verifyCallback to do the actual cert chain verification.
   *
   * @param store_ctx the store context
   * @param ssl_extended_info the info for storing the validation status
   * @param leaf_cert the peer certificate to verify
   * @return 1 to indicate verification success and 0 to indicate verification failure.
   */
  virtual int
  doVerifyCertChain(X509_STORE_CTX* store_ctx, Ssl::SslExtendedSocketInfo* ssl_extended_info,
                    X509& leaf_cert,
                    const Network::TransportSocketOptions* transport_socket_options) PURE;

  /**
   * Called to initialize all ssl contexts
   *
   * @param contexts the store context
   * @param handshaker_provides_certificates whether or not a handshaker implementation provides
   * certificates itself.
   * @return the ssl verification mode flag
   */
  virtual int initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                    bool handshaker_provides_certificates) PURE;

  /**
   * Called when calculation hash for session context ids
   *
   * @param md the store context
   * @param hash_buffer the buffer used for digest calculation
   * @param hash_length the expected length of hash
   */
  virtual void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md,
                                        uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                        unsigned hash_length) PURE;

  virtual size_t daysUntilFirstCertExpires() const PURE;
  virtual std::string getCaFileName() const PURE;
  virtual Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const PURE;
};

using CertValidatorPtr = std::unique_ptr<CertValidator>;

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

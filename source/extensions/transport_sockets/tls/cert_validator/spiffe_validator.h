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

#include "common/common/c_smart_ptr.h"
#include "common/common/matchers.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/transport_sockets/tls/cert_validator/cert_validator.h"
#include "extensions/transport_sockets/tls/stats.h"

#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using X509StorePtr = CSmartPtr<X509_STORE, X509_STORE_free>;

class SPIFFEValidator : public CertValidator {
public:
  SPIFFEValidator(Envoy::Ssl::CertificateValidationContextConfig* config);
  ~SPIFFEValidator() override = default;

  // Tls::CertValidator
  void addClientValidationContext(SSL_CTX* context, bool require_client_cert) override;

  int doVerifyCertChain(X509_STORE_CTX* store_ctx, Ssl::SslExtendedSocketInfo* ssl_extended_info,
                        X509& leaf_cert,
                        const Network::TransportSocketOptions* transport_socket_options) override;

  int initializeSslContexts(std::vector<SSL_CTX*> contexts, bool provides_certificates) override;

  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md, uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                unsigned hash_length) override;

  size_t daysUntilFirstCertExpires() const override;
  std::string getCaFileName() const override;
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;

  // utility functions
  static std::string extractTrustDomain(const std::string& san);
  static int certificatePrecheck(X509* leaf_cert);

private:
  absl::flat_hash_map<std::string, X509StorePtr> trust_bundle_stores_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

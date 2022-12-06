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

#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/matchers.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/transport_sockets/tls/cert_validator/cert_validator.h"
#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher.h"
#include "source/extensions/transport_sockets/tls/stats.h"

#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using X509StorePtr = CSmartPtr<X509_STORE, X509_STORE_free>;

class SPIFFEValidator : public CertValidator {
public:
  SPIFFEValidator(SslStats& stats, TimeSource& time_source)
      : stats_(stats), time_source_(time_source){};
  SPIFFEValidator(const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
                  TimeSource& time_source);
  ~SPIFFEValidator() override = default;

  // Tls::CertValidator
  void addClientValidationContext(SSL_CTX* context, bool require_client_cert) override;

  int doSynchronousVerifyCertChain(
      X509_STORE_CTX* store_ctx, Ssl::SslExtendedSocketInfo* ssl_extended_info, X509& leaf_cert,
      const Network::TransportSocketOptions* transport_socket_options) override;
  ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    Ssl::SslExtendedSocketInfo* ssl_extended_info,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx,
                    const CertValidator::ExtraValidationContext& validation_context, bool is_server,
                    absl::string_view host_name) override;

  int initializeSslContexts(std::vector<SSL_CTX*> contexts, bool provides_certificates) override;

  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md, uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                unsigned hash_length) override;

  absl::optional<uint32_t> daysUntilFirstCertExpires() const override;
  std::string getCaFileName() const override { return ca_file_name_; }
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;

  // Utility functions
  X509_STORE* getTrustBundleStore(X509* leaf_cert);
  static std::string extractTrustDomain(const std::string& san);
  static bool certificatePrecheck(X509* leaf_cert);
  absl::flat_hash_map<std::string, X509StorePtr>& trustBundleStores() {
    return trust_bundle_stores_;
  };

  bool matchSubjectAltName(X509& leaf_cert);

private:
  bool verifyCertChainUsingTrustBundleStore(Ssl::SslExtendedSocketInfo* ssl_extended_info,
                                            X509& leaf_cert, STACK_OF(X509)* cert_chain,
                                            X509_VERIFY_PARAM* verify_param,
                                            std::string& error_details);

  bool allow_expired_certificate_{false};
  std::vector<bssl::UniquePtr<X509>> ca_certs_;
  std::string ca_file_name_;
  std::vector<SanMatcherPtr> subject_alt_name_matchers_{};
  absl::flat_hash_map<std::string, X509StorePtr> trust_bundle_stores_;

  SslStats& stats_;
  TimeSource& time_source_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

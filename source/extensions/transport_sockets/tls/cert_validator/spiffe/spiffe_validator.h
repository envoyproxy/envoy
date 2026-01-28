#pragma once

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/config/datasource.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/cert_validator/san_matcher.h"
#include "source/common/tls/stats.h"

#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using X509StorePtr = CSmartPtr<X509_STORE, X509_STORE_free>;

struct SpiffeData {
  absl::flat_hash_map<std::string, CSmartPtr<X509_STORE, X509_STORE_free>> trust_bundle_stores_;
  std::vector<bssl::UniquePtr<X509>> ca_certs_;
};

class SPIFFEValidator : public CertValidator, Logger::Loggable<Logger::Id::secret> {
public:
  SPIFFEValidator(SslStats& stats, Server::Configuration::CommonFactoryContext& context)
      : spiffe_data_(std::make_shared<SpiffeData>()), stats_(stats),
        time_source_(context.timeSource()) {};
  SPIFFEValidator(const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
                  Server::Configuration::CommonFactoryContext& context, Stats::Scope& scope,
                  absl::Status& creation_status);

  ~SPIFFEValidator() override = default;

  // Tls::CertValidator
  absl::Status addClientValidationContext(SSL_CTX* context, bool require_client_cert) override;

  ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx,
                    const CertValidator::ExtraValidationContext& validation_context, bool is_server,
                    absl::string_view host_name) override;

  absl::StatusOr<int> initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                            bool provides_certificates,
                                            Stats::Scope& scope) override;

  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md, uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                unsigned hash_length) override;
  absl::optional<uint32_t> daysUntilFirstCertExpires() const override;
  std::string getCaFileName() const override { return ca_file_name_; }
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;

  // Utility functions
  X509_STORE* getTrustBundleStore(X509* leaf_cert);
  static std::string extractTrustDomain(const std::string& san);
  static bool certificatePrecheck(X509* leaf_cert);
  OptRef<SpiffeData> getSpiffeData() const {
    if (bundle_provider_) {
      return makeOptRefFromPtr(bundle_provider_->data().get());
    }
    return makeOptRefFromPtr(spiffe_data_.get());
  };
  bool matchSubjectAltName(X509& leaf_cert);

private:
  bool verifyCertChainUsingTrustBundleStore(X509& leaf_cert, STACK_OF(X509)* cert_chain,
                                            X509_VERIFY_PARAM* verify_param,
                                            std::string& error_details);

  void initializeCertExpirationStats(Stats::Scope& scope, const std::string& cert_name);

  bool allow_expired_certificate_{false};

  std::string ca_file_name_;
  std::shared_ptr<SpiffeData> spiffe_data_;
  std::vector<SanMatcherPtr> subject_alt_name_matchers_{};
  SslStats& stats_;
  TimeSource& time_source_;
  using SpiffeTrustBundles = Config::DataSource::ProviderSingleton<SpiffeData>;
  std::shared_ptr<SpiffeTrustBundles> bundle_map_;
  Config::DataSource::DataSourceProviderSharedPtr<SpiffeData> bundle_provider_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

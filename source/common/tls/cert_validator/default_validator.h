#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/cert_validator/san_matcher.h"
#include "source/common/tls/stats.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "openssl/sha.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class CrlCache;

// Holds the parsed CRLs for a single CRL PEM blob. A PEM blob may contain more
// than one CRL, so they are kept as a list. Instances are shared (via
// shared_ptr) between every TLS context that references identical CRL content,
// so the parsed structures - which can be tens of megabytes for CRLs with many
// revoked entries - are materialized in memory only once. A shared_ptr to the
// owning cache is held so that holding a CrlListSharedPtr alone is enough to
// keep both the parsed CRLs and the cache alive.
struct CrlList {
  std::vector<bssl::UniquePtr<X509_CRL>> crls;
  std::shared_ptr<CrlCache> cache;
};
using CrlListSharedPtr = std::shared_ptr<CrlList>;

// Process-wide cache that parses each distinct CRL blob once and shares the
// parsed representation across all TLS contexts that reference it. Without this,
// a CRL referenced from many `common_tls_context`s is parsed and held in memory
// once per context, which can consume a large amount of memory for big CRLs.
//
// Threading and lifetime model (mirrors SharedPool::ObjectSharedPool):
//   - All methods must be called on the main (or test) thread. TLS context
//     creation, the only caller, is confined to that thread.
//   - Only a weak_ptr is stored, so an entry is released as soon as the last
//     CrlList referencing it is destroyed (for example after an xDS update).
//     Each returned CrlList holds a shared_ptr back to this cache, so the cache
//     outlives every entry handed out from it.
//   - The parsed X509_CRLs are reference counted by BoringSSL; each X509_STORE a
//     CRL is added to holds its own reference, so it remains valid for that
//     store's lifetime independent of this cache.
class CrlCache : public Singleton::Instance, public std::enable_shared_from_this<CrlCache> {
public:
  // Returns the shared parsed representation of `crl_pem`, parsing and caching
  // it on first use. `crl_path` is only used to build the error message.
  // Returns an error if `crl_pem` cannot be parsed.
  absl::StatusOr<CrlListSharedPtr> getOrCreate(const std::string& crl_pem,
                                               const std::string& crl_path);

  // Number of distinct CRL blobs currently referenced by at least one context.
  // Exposed for testing.
  size_t size() const;

private:
  // Keyed by a SHA-256 digest of the CRL PEM (rather than the PEM itself, to
  // avoid holding a second full copy of potentially large CRL data); stores a
  // weak_ptr so entries do not outlive the contexts that use them.
  absl::flat_hash_map<std::array<uint8_t, SHA256_DIGEST_LENGTH>, std::weak_ptr<CrlList>> cache_;
};

// Returns the process-wide CRL cache, creating it on first use.
std::shared_ptr<CrlCache> getCrlCache(Singleton::Manager& singleton_manager);

class DefaultCertValidator : public CertValidator, Logger::Loggable<Logger::Id::connection> {
public:
  DefaultCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                       SslStats& stats, Server::Configuration::CommonFactoryContext& context);

  ~DefaultCertValidator() override = default;

  // Tls::CertValidator
  absl::Status addClientValidationContext(SSL_CTX* context, bool require_client_cert) override;

  ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl, const CertValidator::ExtraValidationContext& validation_context,
                    bool is_server, absl::string_view host_name) override;

  absl::StatusOr<int> initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                            bool provides_certificates,
                                            Stats::Scope& scope) override;

  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md, uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                unsigned hash_length) override;

  std::optional<uint32_t> daysUntilFirstCertExpires() const override;
  std::string getCaFileName() const override { return ca_file_path_; };
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;

  // Utility functions.
  Envoy::Ssl::ClientValidationStatus
  verifyCertificate(X509* cert, const std::vector<std::string>& verify_san_list,
                    const std::vector<SanMatcherPtr>& subject_alt_name_matchers,
                    OptRef<const StreamInfo::StreamInfo> stream_info, std::string* error_details,
                    uint8_t* out_alert);

  /**
   * Verifies certificate hash for pinning. The hash is a hex-encoded SHA-256 of the DER-encoded
   * certificate.
   *
   * @param ssl the certificate to verify
   * @param expected_hashes the configured list of certificate hashes to match
   * @return true if the verification succeeds
   */
  static bool verifyCertificateHashList(X509* cert,
                                        const std::vector<std::vector<uint8_t>>& expected_hashes);

  /**
   * Verifies certificate hash for pinning. The hash is a base64-encoded SHA-256 of the DER-encoded
   * Subject Public Key Information (SPKI) of the certificate.
   *
   * @param ssl the certificate to verify
   * @param expected_hashes the configured list of certificate hashes to match
   * @return true if the verification succeeds
   */
  static bool verifyCertificateSpkiList(X509* cert,
                                        const std::vector<std::vector<uint8_t>>& expected_hashes);

  /**
   * Performs subjectAltName verification
   * @param ssl the certificate to verify
   * @param subject_alt_names the configured subject_alt_names to match
   * @return true if the verification succeeds
   */
  static bool verifySubjectAltName(X509* cert, const std::vector<std::string>& subject_alt_names);

  /**
   * Performs subjectAltName matching with the provided matchers.
   * @param ssl the certificate to verify
   * @param subject_alt_name_matchers the configured matchers to match
   * @return true if the verification succeeds
   */
  static bool matchSubjectAltName(X509* cert, OptRef<const StreamInfo::StreamInfo> stream_info,
                                  const std::vector<SanMatcherPtr>& subject_alt_name_matchers);

private:
  bool verifyCertAndUpdateStatus(X509* leaf_cert, absl::string_view sni,
                                 const Network::TransportSocketOptions* transport_socket_options,
                                 const CertValidator::ExtraValidationContext& validation_context,
                                 Envoy::Ssl::ClientValidationStatus& detailed_status,
                                 std::string* error_details, uint8_t* out_alert);

  void initializeCertExpirationStats(Stats::Scope& scope);
  const Envoy::Ssl::CertificateValidationContextConfig* config_;
  SslStats& stats_;
  Server::Configuration::CommonFactoryContext& context_;
  bssl::UniquePtr<X509> ca_cert_;
  std::string ca_file_path_;
  std::vector<SanMatcherPtr> subject_alt_name_matchers_;
  std::vector<std::vector<uint8_t>> verify_certificate_hash_list_;
  std::vector<std::vector<uint8_t>> verify_certificate_spki_list_;
  // The parsed CRLs shared with other TLS contexts that reference the same CRL.
  // This also keeps the CRL cache alive for as long as the validator uses it.
  CrlListSharedPtr shared_crl_;
  bool allow_untrusted_certificate_{false};
  bool verify_trusted_ca_{false};
  const bool auto_sni_san_match_{false};
};

DECLARE_FACTORY(DefaultCertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

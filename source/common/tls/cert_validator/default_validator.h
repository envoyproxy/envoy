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
#include "source/common/common/thread.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/cert_validator/san_matcher.h"
#include "source/common/tls/stats.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "openssl/sha.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Process-wide cache that shares one parsed copy of each distinct PEM blob across all TLS contexts.
// Entries are keyed by the PEM's SHA-256 digest and held as weak_ptrs.
//   - Lookups and inserts run on the main thread only.
//   - Each entry's deleter erases its key when the last reference is released. That can happen on
//     any thread, so the map is guarded by a mutex.
//   - BoringSSL reference counts the parsed objects, so each SSL_CTX or X509_STORE keeps them valid
//     independent of this cache.
template <class T>
class SharedPemCache : public Singleton::Instance,
                       public std::enable_shared_from_this<SharedPemCache<T>> {
public:
  using Key = std::array<uint8_t, SHA256_DIGEST_LENGTH>;

  // Number of distinct entries currently referenced by at least one context. Exposed for testing.
  size_t size() const {
    absl::MutexLock lock(mutex_);
    return entries_.size();
  }

protected:
  // Returns the shared entry for `pem`, calling `parse` and caching its result on first use. A
  // parse error is returned to the caller and nothing is cached.
  absl::StatusOr<std::shared_ptr<T>>
  getOrParse(absl::string_view pem, absl::FunctionRef<absl::StatusOr<std::unique_ptr<T>>()> parse) {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    Key key;
    SHA256(reinterpret_cast<const uint8_t*>(pem.data()), pem.size(), key.data());
    {
      absl::MutexLock lock(mutex_);
      if (auto it = entries_.find(key); it != entries_.end()) {
        if (std::shared_ptr<T> existing = it->second.lock(); existing != nullptr) {
          return existing;
        }
      }
    }

    // Parse without holding the lock. Inserts only happen on the main thread, so no other
    // caller can add this key in the meantime.
    absl::StatusOr<std::unique_ptr<T>> parsed = parse();
    if (!parsed.ok()) {
      return parsed.status();
    }
    std::shared_ptr<T> entry(parsed->release(), Releaser{this->shared_from_this(), key});
    absl::MutexLock lock(mutex_);
    entries_[key] = entry;
    ENVOY_LOG_MISC(debug, "tls: cached parsed PEM entry, {} entries", entries_.size());
    return entry;
  }

private:
  // Deleter for entries handed out by getOrParse(). Holding the cache keeps it alive for as long
  // as any of its entries is referenced.
  struct Releaser {
    std::shared_ptr<SharedPemCache<T>> cache;
    Key key;

    void operator()(T* value) const {
      cache->erase(key);
      delete value;
    }
  };

  // Erases `key` unless it has already been replaced by a live entry, which happens when the key
  // is re-added on the main thread while the previous entry's last reference is being released.
  void erase(const Key& key) {
    absl::MutexLock lock(mutex_);
    if (auto it = entries_.find(key); it != entries_.end() && it->second.expired()) {
      entries_.erase(it);
      ENVOY_LOG_MISC(debug, "tls: released parsed PEM entry, {} entries", entries_.size());
    }
  }

  mutable absl::Mutex mutex_;
  absl::flat_hash_map<Key, std::weak_ptr<T>> entries_ ABSL_GUARDED_BY(mutex_);
};

// Holds the parsed CRLs for a single CRL PEM blob. A PEM blob may contain more
// than one CRL, so they are kept as a list. Instances are shared (via
// shared_ptr) between every TLS context that references identical CRL content,
// so the parsed structures - which can be tens of megabytes for CRLs with many
// revoked entries - are materialized in memory only once.
struct CrlList {
  std::vector<bssl::UniquePtr<X509_CRL>> crls;
};
using CrlListSharedPtr = std::shared_ptr<CrlList>;

// Parses each distinct CRL blob once and shares the parsed representation across all TLS contexts
// that reference it. Without this, a CRL referenced from many `common_tls_context`s is parsed and
// held in memory once per context, which can consume a large amount of memory for big CRLs.
class CrlCache : public SharedPemCache<CrlList> {
public:
  // Returns the shared parsed representation of `crl_pem`, parsing and caching
  // it on first use. `crl_path` is only used to build the error message.
  // Returns an error if `crl_pem` cannot be parsed.
  absl::StatusOr<CrlListSharedPtr> getOrCreate(const std::string& crl_pem,
                                               const std::string& crl_path);
};

// Returns the process-wide CRL cache, creating it on first use.
std::shared_ptr<CrlCache> getCrlCache(Singleton::Manager& singleton_manager);

// Holds the certificates - and any CRLs, since a trusted CA PEM blob is allowed
// to carry both - parsed from a single trusted CA PEM blob. Instances are shared
// (via shared_ptr) between every TLS context that references identical CA
// content, so the parsed X509 structures, which for a large trust bundle
// dominate a context's memory, are materialized in memory only once.
struct CaCertList {
  std::vector<bssl::UniquePtr<X509>> certs;
  std::vector<bssl::UniquePtr<X509_CRL>> crls;
};
using CaCertListSharedPtr = std::shared_ptr<CaCertList>;

// Parses each distinct trusted CA blob once and shares the parsed representation across all TLS
// contexts that reference it. Without this, a trust bundle referenced from many
// `common_tls_context`s is parsed and held in memory once per context. That is the common shape for
// upstream clusters, which frequently share a single trust root. Only the immutable parsed material
// is shared; each context keeps its own X509_STORE and therefore its own store flags.
class CaCertCache : public SharedPemCache<CaCertList> {
public:
  // Returns the shared parsed representation of `ca_pem`, parsing and caching it
  // on first use. `ca_path` is only used to build the error message. Returns an
  // error if `ca_pem` cannot be parsed or contains no certificate.
  absl::StatusOr<CaCertListSharedPtr> getOrCreate(const std::string& ca_pem,
                                                  const std::string& ca_path);
};

// Returns the process-wide trusted CA cache, creating it on first use.
std::shared_ptr<CaCertCache> getCaCertCache(Singleton::Manager& singleton_manager);

// Holds the parsed local certificate chain - the leaf plus any intermediate
// certificates - from a single certificate-chain PEM blob. Instances are shared
// (via shared_ptr) between every TLS context that references identical chain
// content, so the parsed X509 structures are materialized in memory only once
// and the PEM is parsed only once. This is the common shape for a cluster that
// carries a distinct client certificate per endpoint: the same chain is
// referenced from many transport socket matches, and every xDS resend of the
// cluster re-parses all of them.
struct CertChain {
  bssl::UniquePtr<X509> leaf;
  std::vector<bssl::UniquePtr<X509>> intermediates;
};
using CertChainSharedPtr = std::shared_ptr<CertChain>;

// Parses each distinct certificate-chain blob once and shares the parsed representation across all
// TLS contexts that reference it.
class CertChainCache : public SharedPemCache<CertChain> {
public:
  // Returns the shared parsed representation of `cert_pem`, parsing and caching
  // it on first use. `cert_path` is only used to build the error message.
  // Returns an error if `cert_pem` cannot be parsed or carries no certificate.
  absl::StatusOr<CertChainSharedPtr> getOrCreate(const std::string& cert_pem,
                                                 const std::string& cert_path);
};

// Returns the process-wide certificate-chain cache, creating it on first use.
std::shared_ptr<CertChainCache> getCertChainCache(Singleton::Manager& singleton_manager);

// Holds a parsed private key from a single private-key PEM blob, together with
// whether it has already passed the FIPS pairwise consistency check (run once
// per distinct key rather than once per context). Instances are shared (via
// shared_ptr) between every TLS context that references identical key content.
struct ParsedPrivateKey {
  bssl::UniquePtr<EVP_PKEY> pkey;
  // Set once the key has passed the FIPS pairwise check; skipped on later hits.
  // FIPS mode is fixed for the process, so a single validation is sufficient.
  bool fips_validated{false};
};
using ParsedPrivateKeySharedPtr = std::shared_ptr<ParsedPrivateKey>;

// Parses each distinct private-key blob once and shares the parsed key across all TLS contexts that
// reference it. Only unencrypted (no-password) keys are cached; the private-key-method-provider
// path never reaches this cache.
class PrivateKeyCache : public SharedPemCache<ParsedPrivateKey> {
public:
  // Returns the shared parsed representation of `key_pem`, parsing and caching it
  // on first use. `key_path` is only used to build the error message. Returns an
  // error if `key_pem` cannot be parsed.
  absl::StatusOr<ParsedPrivateKeySharedPtr> getOrCreate(const std::string& key_pem,
                                                        const std::string& key_path);
};

// Returns the process-wide private-key cache, creating it on first use.
std::shared_ptr<PrivateKeyCache> getPrivateKeyCache(Singleton::Manager& singleton_manager);

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
  std::vector<Envoy::Ssl::CertificateDetailsPtr> getCaCertInformation() const override;

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
  static bool verifySubjectAltName(X509* cert, absl::Span<const std::string> subject_alt_names);

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
  std::string ca_file_path_;
  std::vector<SanMatcherPtr> subject_alt_name_matchers_;
  std::vector<std::vector<uint8_t>> verify_certificate_hash_list_;
  std::vector<std::vector<uint8_t>> verify_certificate_spki_list_;
  // The parsed CRLs shared with other TLS contexts that reference the same CRL.
  // This also keeps the CRL cache alive for as long as the validator uses it.
  CrlListSharedPtr shared_crl_;
  // The parsed trusted CA certificates shared with other TLS contexts that
  // reference the same CA blob. This also keeps the CA cache alive for as long
  // as the validator uses it.
  CaCertListSharedPtr shared_ca_certs_;
  bool allow_untrusted_certificate_{false};
  bool verify_trusted_ca_{false};
  const bool auto_sni_san_match_{false};
};

DECLARE_FACTORY(DefaultCertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/common/matchers.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/extensions/transport_sockets/tls/cert_validator/cert_validator.h"
#include "source/extensions/transport_sockets/tls/cert_validator/factory.h"
#include "source/extensions/transport_sockets/tls/cert_validator/utility.h"
#include "source/extensions/transport_sockets/tls/stats.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

DefaultCertValidator::DefaultCertValidator(
    const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
    TimeSource& time_source)
    : config_(config), stats_(stats), time_source_(time_source) {
  if (config_ != nullptr) {
    allow_untrusted_certificate_ = config_->trustChainVerification() ==
                                   envoy::extensions::transport_sockets::tls::v3::
                                       CertificateValidationContext::ACCEPT_UNTRUSTED;
  }
};

int DefaultCertValidator::initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                                bool provides_certificates) {

  int verify_mode = SSL_VERIFY_NONE;
  int verify_mode_validation_context = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

  if (config_ != nullptr) {
    envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
        TrustChainVerification verification = config_->trustChainVerification();
    if (verification == envoy::extensions::transport_sockets::tls::v3::
                            CertificateValidationContext::ACCEPT_UNTRUSTED) {
      verify_mode = SSL_VERIFY_PEER; // Ensure client-certs will be requested even if we have
                                     // nothing to verify against
      verify_mode_validation_context = SSL_VERIFY_PEER;
    }
  }

  if (config_ != nullptr && !config_->caCert().empty() && !provides_certificates) {
    ca_file_path_ = config_->caCertPath();
    bssl::UniquePtr<BIO> bio(
        BIO_new_mem_buf(const_cast<char*>(config_->caCert().data()), config_->caCert().size()));
    RELEASE_ASSERT(bio != nullptr, "");
    // Based on BoringSSL's X509_load_cert_crl_file().
    bssl::UniquePtr<STACK_OF(X509_INFO)> list(
        PEM_X509_INFO_read_bio(bio.get(), nullptr, nullptr, nullptr));
    if (list == nullptr) {
      throw EnvoyException(
          absl::StrCat("Failed to load trusted CA certificates from ", config_->caCertPath()));
    }

    for (auto& ctx : contexts) {
      X509_STORE* store = SSL_CTX_get_cert_store(ctx);
      bool has_crl = false;
      for (const X509_INFO* item : list.get()) {
        if (item->x509) {
          X509_STORE_add_cert(store, item->x509);
          if (ca_cert_ == nullptr) {
            X509_up_ref(item->x509);
            ca_cert_.reset(item->x509);
          }
        }
        if (item->crl) {
          X509_STORE_add_crl(store, item->crl);
          has_crl = true;
        }
      }
      if (ca_cert_ == nullptr) {
        throw EnvoyException(
            absl::StrCat("Failed to load trusted CA certificates from ", config_->caCertPath()));
      }
      if (has_crl) {
        X509_STORE_set_flags(store, config_->onlyVerifyLeafCertificateCrl()
                                        ? X509_V_FLAG_CRL_CHECK
                                        : X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
      }
      verify_mode = SSL_VERIFY_PEER;
      verify_trusted_ca_ = true;

      if (config_->allowExpiredCertificate()) {
        CertValidatorUtil::setIgnoreCertificateExpiration(store);
      }
    }
  }

  if (config_ != nullptr && !config_->certificateRevocationList().empty()) {
    bssl::UniquePtr<BIO> bio(
        BIO_new_mem_buf(const_cast<char*>(config_->certificateRevocationList().data()),
                        config_->certificateRevocationList().size()));
    RELEASE_ASSERT(bio != nullptr, "");

    // Based on BoringSSL's X509_load_cert_crl_file().
    bssl::UniquePtr<STACK_OF(X509_INFO)> list(
        PEM_X509_INFO_read_bio(bio.get(), nullptr, nullptr, nullptr));
    if (list == nullptr) {
      throw EnvoyException(
          absl::StrCat("Failed to load CRL from ", config_->certificateRevocationListPath()));
    }

    for (auto& ctx : contexts) {
      X509_STORE* store = SSL_CTX_get_cert_store(ctx);
      for (const X509_INFO* item : list.get()) {
        if (item->crl) {
          X509_STORE_add_crl(store, item->crl);
        }
      }
      X509_STORE_set_flags(store, config_->onlyVerifyLeafCertificateCrl()
                                      ? X509_V_FLAG_CRL_CHECK
                                      : X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
    }
  }

  const Envoy::Ssl::CertificateValidationContextConfig* cert_validation_config = config_;
  if (cert_validation_config != nullptr) {
    if (!cert_validation_config->subjectAltNameMatchers().empty()) {
      for (const envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher& matcher :
           cert_validation_config->subjectAltNameMatchers()) {
        auto san_matcher = createStringSanMatcher(matcher);
        if (san_matcher == nullptr) {
          throw EnvoyException(
              absl::StrCat("Failed to create string SAN matcher of type ", matcher.san_type()));
        }
        subject_alt_name_matchers_.push_back(std::move(san_matcher));
      }
      verify_mode = verify_mode_validation_context;
    }

    if (!cert_validation_config->verifyCertificateHashList().empty()) {
      for (auto hash : cert_validation_config->verifyCertificateHashList()) {
        // Remove colons from the 95 chars long colon-separated "fingerprint"
        // in order to get the hex-encoded string.
        if (hash.size() == 95) {
          hash.erase(std::remove(hash.begin(), hash.end(), ':'), hash.end());
        }
        const auto& decoded = Hex::decode(hash);
        if (decoded.size() != SHA256_DIGEST_LENGTH) {
          throw EnvoyException(absl::StrCat("Invalid hex-encoded SHA-256 ", hash));
        }
        verify_certificate_hash_list_.push_back(decoded);
      }
      verify_mode = verify_mode_validation_context;
    }

    if (!cert_validation_config->verifyCertificateSpkiList().empty()) {
      for (const auto& hash : cert_validation_config->verifyCertificateSpkiList()) {
        const auto decoded = Base64::decode(hash);
        if (decoded.size() != SHA256_DIGEST_LENGTH) {
          throw EnvoyException(absl::StrCat("Invalid base64-encoded SHA-256 ", hash));
        }
        verify_certificate_spki_list_.emplace_back(decoded.begin(), decoded.end());
      }
      verify_mode = verify_mode_validation_context;
    }
  }

  return verify_mode;
}

int DefaultCertValidator::doSynchronousVerifyCertChain(
    X509_STORE_CTX* store_ctx, Ssl::SslExtendedSocketInfo* ssl_extended_info, X509& leaf_cert,
    const Network::TransportSocketOptions* transport_socket_options) {
  if (verify_trusted_ca_) {
    int ret = X509_verify_cert(store_ctx);
    if (ssl_extended_info) {
      ssl_extended_info->setCertificateValidationStatus(
          ret == 1 ? Envoy::Ssl::ClientValidationStatus::Validated
                   : Envoy::Ssl::ClientValidationStatus::Failed);
    }

    if (ret <= 0) {
      stats_.fail_verify_error_.inc();
      ENVOY_LOG(debug, "{}", Utility::getX509VerificationErrorInfo(store_ctx));
      return allow_untrusted_certificate_ ? 1 : ret;
    }
  }
  if (!verifyCertAndUpdateStatus(ssl_extended_info, &leaf_cert, transport_socket_options, nullptr,
                                 nullptr)) {
    X509_STORE_CTX_set_error(store_ctx, X509_V_ERR_APPLICATION_VERIFICATION);
    return 0;
  }
  return 1;
}

bool DefaultCertValidator::verifyCertAndUpdateStatus(
    Ssl::SslExtendedSocketInfo* ssl_extended_info, X509* leaf_cert,
    const Network::TransportSocketOptions* transport_socket_options, std::string* error_details,
    uint8_t* out_alert) {
  Envoy::Ssl::ClientValidationStatus validated =
      verifyCertificate(leaf_cert,
                        transport_socket_options != nullptr
                            ? transport_socket_options->verifySubjectAltNameListOverride()
                            : std::vector<std::string>{},
                        subject_alt_name_matchers_, error_details, out_alert);

  if (ssl_extended_info) {
    if (ssl_extended_info->certificateValidationStatus() ==
        Envoy::Ssl::ClientValidationStatus::NotValidated) {
      ssl_extended_info->setCertificateValidationStatus(validated);
    } else if (validated != Envoy::Ssl::ClientValidationStatus::NotValidated) {
      ssl_extended_info->setCertificateValidationStatus(validated);
    }
  }

  // If `trusted_ca` exists, it is already verified in the code above. Thus, we just need to make
  // sure the verification for other validation context configurations doesn't fail (i.e. either
  // `NotValidated` or `Validated`). If `trusted_ca` doesn't exist, we will need to make sure
  // other configurations are verified and the verification succeed.
  const bool success = verify_trusted_ca_
                           ? validated != Envoy::Ssl::ClientValidationStatus::Failed
                           : validated == Envoy::Ssl::ClientValidationStatus::Validated;

  return (allow_untrusted_certificate_ || success);
}

Envoy::Ssl::ClientValidationStatus
DefaultCertValidator::verifyCertificate(X509* cert, const std::vector<std::string>& verify_san_list,
                                        const std::vector<SanMatcherPtr>& subject_alt_name_matchers,
                                        std::string* error_details, uint8_t* out_alert) {
  Envoy::Ssl::ClientValidationStatus validated = Envoy::Ssl::ClientValidationStatus::NotValidated;
  if (!verify_san_list.empty()) {
    if (!verifySubjectAltName(cert, verify_san_list)) {
      const char* error = "verify cert failed: verify SAN list";
      if (error_details != nullptr) {
        *error_details = error;
      }
      ENVOY_LOG(debug, error);
      stats_.fail_verify_san_.inc();
      return Envoy::Ssl::ClientValidationStatus::Failed;
    }
    validated = Envoy::Ssl::ClientValidationStatus::Validated;
  }

  if (!subject_alt_name_matchers.empty()) {
    if (!matchSubjectAltName(cert, subject_alt_name_matchers)) {
      const char* error = "verify cert failed: SAN matcher";
      if (error_details != nullptr) {
        *error_details = error;
      }
      ENVOY_LOG(debug, error);
      stats_.fail_verify_san_.inc();
      return Envoy::Ssl::ClientValidationStatus::Failed;
    }
    validated = Envoy::Ssl::ClientValidationStatus::Validated;
  }

  if (!verify_certificate_hash_list_.empty() || !verify_certificate_spki_list_.empty()) {
    const bool valid_certificate_hash =
        !verify_certificate_hash_list_.empty() &&
        verifyCertificateHashList(cert, verify_certificate_hash_list_);
    const bool valid_certificate_spki =
        !verify_certificate_spki_list_.empty() &&
        verifyCertificateSpkiList(cert, verify_certificate_spki_list_);

    if (!valid_certificate_hash && !valid_certificate_spki) {
      if (out_alert != nullptr) {
        *out_alert = SSL_AD_BAD_CERTIFICATE_HASH_VALUE;
      }
      const char* error = "verify cert failed: cert hash and spki";
      if (error_details != nullptr) {
        *error_details = error;
      }
      ENVOY_LOG(debug, error);
      stats_.fail_verify_cert_hash_.inc();
      return Envoy::Ssl::ClientValidationStatus::Failed;
    }

    validated = Envoy::Ssl::ClientValidationStatus::Validated;
  }

  return validated;
}

ValidationResults DefaultCertValidator::doVerifyCertChain(
    STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr /*callback*/,
    Ssl::SslExtendedSocketInfo* ssl_extended_info,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options, SSL_CTX& ssl_ctx,
    const CertValidator::ExtraValidationContext& /*validation_context*/, bool is_server,
    absl::string_view /*host_name*/) {
  if (sk_X509_num(&cert_chain) == 0) {
    if (ssl_extended_info) {
      ssl_extended_info->setCertificateValidationStatus(
          Envoy::Ssl::ClientValidationStatus::NotValidated);
    }
    stats_.fail_verify_error_.inc();
    const char* error = "verify cert failed: empty cert chain";
    ENVOY_LOG(debug, error);
    return {ValidationResults::ValidationStatus::Failed, absl::nullopt, error};
  }
  X509* leaf_cert = sk_X509_value(&cert_chain, 0);
  ASSERT(leaf_cert);
  if (verify_trusted_ca_) {
    X509_STORE* verify_store = SSL_CTX_get_cert_store(&ssl_ctx);
    ASSERT(verify_store);
    bssl::UniquePtr<X509_STORE_CTX> ctx(X509_STORE_CTX_new());
    if (!ctx || !X509_STORE_CTX_init(ctx.get(), verify_store, leaf_cert, &cert_chain) ||
        // We need to inherit the verify parameters. These can be determined by
        // the context: if it's a server it will verify SSL client certificates or
        // vice versa.
        !X509_STORE_CTX_set_default(ctx.get(), is_server ? "ssl_client" : "ssl_server") ||
        // Anything non-default in "param" should overwrite anything in the ctx.
        !X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(ctx.get()),
                                SSL_CTX_get0_param(&ssl_ctx))) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_X509_LIB);
      const char* error = "verify cert failed: init and setup X509_STORE_CTX";
      onVerifyError(ssl_extended_info, error);
      return {ValidationResults::ValidationStatus::Failed, absl::nullopt, error};
    }
    const bool verify_succeeded = (X509_verify_cert(ctx.get()) == 1);

    if (!verify_succeeded) {
      const std::string error =
          absl::StrCat("verify cert failed: ", Utility::getX509VerificationErrorInfo(ctx.get()));
      onVerifyError(ssl_extended_info, error);
      if (allow_untrusted_certificate_) {
        return ValidationResults{ValidationResults::ValidationStatus::Successful, absl::nullopt,
                                 absl::nullopt};
      }
      return {ValidationResults::ValidationStatus::Failed,
              SSL_alert_from_verify_result(X509_STORE_CTX_get_error(ctx.get())), error};
    }
    if (ssl_extended_info) {
      ssl_extended_info->setCertificateValidationStatus(
          Envoy::Ssl::ClientValidationStatus::Validated);
    }
  }
  std::string error_details;
  uint8_t tls_alert = SSL_AD_CERTIFICATE_UNKNOWN;
  const bool succeeded = verifyCertAndUpdateStatus(
      ssl_extended_info, leaf_cert, transport_socket_options.get(), &error_details, &tls_alert);
  return succeeded ? ValidationResults{ValidationResults::ValidationStatus::Successful,
                                       absl::nullopt, absl::nullopt}
                   : ValidationResults{ValidationResults::ValidationStatus::Failed, tls_alert,
                                       error_details};
}

void DefaultCertValidator::onVerifyError(Ssl::SslExtendedSocketInfo* ssl_extended_info,
                                         absl::string_view error) {
  if (ssl_extended_info) {
    ssl_extended_info->setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus::Failed);
  }
  stats_.fail_verify_error_.inc();
  ENVOY_LOG(debug, error);
}

bool DefaultCertValidator::verifySubjectAltName(X509* cert,
                                                const std::vector<std::string>& subject_alt_names) {
  bssl::UniquePtr<GENERAL_NAMES> san_names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names == nullptr) {
    return false;
  }
  for (const GENERAL_NAME* general_name : san_names.get()) {
    const std::string san = Utility::generalNameAsString(general_name);
    for (auto& config_san : subject_alt_names) {
      if (general_name->type == GEN_DNS ? Utility::dnsNameMatch(config_san, san.c_str())
                                        : config_san == san) {
        return true;
      }
    }
  }
  return false;
}

bool DefaultCertValidator::matchSubjectAltName(
    X509* cert, const std::vector<SanMatcherPtr>& subject_alt_name_matchers) {
  bssl::UniquePtr<GENERAL_NAMES> san_names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names == nullptr) {
    return false;
  }
  for (const auto& config_san_matcher : subject_alt_name_matchers) {
    for (const GENERAL_NAME* general_name : san_names.get()) {
      if (config_san_matcher->match(general_name)) {
        return true;
      }
    }
  }
  return false;
}

bool DefaultCertValidator::verifyCertificateSpkiList(
    X509* cert, const std::vector<std::vector<uint8_t>>& expected_hashes) {
  X509_PUBKEY* pubkey = X509_get_X509_PUBKEY(cert);
  if (pubkey == nullptr) {
    return false;
  }
  uint8_t* spki = nullptr;
  const int len = i2d_X509_PUBKEY(pubkey, &spki);
  if (len < 0) {
    return false;
  }
  bssl::UniquePtr<uint8_t> free_spki(spki);

  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  SHA256(spki, len, computed_hash.data());

  for (const auto& expected_hash : expected_hashes) {
    if (computed_hash == expected_hash) {
      return true;
    }
  }
  return false;
}

bool DefaultCertValidator::verifyCertificateHashList(
    X509* cert, const std::vector<std::vector<uint8_t>>& expected_hashes) {
  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert, EVP_sha256(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size(), "");

  for (const auto& expected_hash : expected_hashes) {
    if (computed_hash == expected_hash) {
      return true;
    }
  }
  return false;
}

void DefaultCertValidator::updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md,
                                                    uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                                    unsigned hash_length) {
  int rc;

  // Hash all the settings that affect whether the server will allow/accept
  // the client connection. This ensures that the client is always validated against
  // the correct settings, even if session resumption across different listeners
  // is enabled.
  if (ca_cert_ != nullptr) {
    rc = X509_digest(ca_cert_.get(), EVP_sha256(), hash_buffer, &hash_length);
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
    RELEASE_ASSERT(hash_length == SHA256_DIGEST_LENGTH,
                   fmt::format("invalid SHA256 hash length {}", hash_length));

    rc = EVP_DigestUpdate(md.get(), hash_buffer, hash_length);
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
  }

  for (const auto& hash : verify_certificate_hash_list_) {
    rc = EVP_DigestUpdate(md.get(), hash.data(),
                          hash.size() *
                              sizeof(std::remove_reference<decltype(hash)>::type::value_type));
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
  }

  for (const auto& hash : verify_certificate_spki_list_) {
    rc = EVP_DigestUpdate(md.get(), hash.data(),
                          hash.size() *
                              sizeof(std::remove_reference<decltype(hash)>::type::value_type));
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
  }

  rc = EVP_DigestUpdate(md.get(), &verify_trusted_ca_, sizeof(verify_trusted_ca_));
  RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

  if (config_ != nullptr) {
    for (const auto& matcher : config_->subjectAltNameMatchers()) {
      size_t hash = MessageUtil::hash(matcher);
      rc = EVP_DigestUpdate(md.get(), &hash, sizeof(hash));
      RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
    }

    const std::string& crl = config_->certificateRevocationList();
    if (!crl.empty()) {
      rc = EVP_DigestUpdate(md.get(), crl.data(), crl.length());
      RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
    }

    bool allow_expired = config_->allowExpiredCertificate();
    rc = EVP_DigestUpdate(md.get(), &allow_expired, sizeof(allow_expired));
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

    auto trust_chain_verification = config_->trustChainVerification();
    rc = EVP_DigestUpdate(md.get(), &trust_chain_verification, sizeof(trust_chain_verification));
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

    auto only_leaf_crl = config_->onlyVerifyLeafCertificateCrl();
    rc = EVP_DigestUpdate(md.get(), &only_leaf_crl, sizeof(only_leaf_crl));
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
  }
}

void DefaultCertValidator::addClientValidationContext(SSL_CTX* ctx, bool require_client_cert) {
  if (config_ == nullptr || config_->caCert().empty()) {
    return;
  }

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(config_->caCert().data()), config_->caCert().size()));
  RELEASE_ASSERT(bio != nullptr, "");
  // Based on BoringSSL's SSL_add_file_cert_subjects_to_stack().
  bssl::UniquePtr<STACK_OF(X509_NAME)> list(sk_X509_NAME_new(
      [](const X509_NAME** a, const X509_NAME** b) -> int { return X509_NAME_cmp(*a, *b); }));
  RELEASE_ASSERT(list != nullptr, "");
  for (;;) {
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (cert == nullptr) {
      break;
    }
    X509_NAME* name = X509_get_subject_name(cert.get());
    if (name == nullptr) {
      throw EnvoyException(absl::StrCat("Failed to load trusted client CA certificates from ",
                                        config_->caCertPath()));
    }
    // Check for duplicates.
    if (sk_X509_NAME_find(list.get(), nullptr, name)) {
      continue;
    }
    bssl::UniquePtr<X509_NAME> name_dup(X509_NAME_dup(name));
    if (name_dup == nullptr || !sk_X509_NAME_push(list.get(), name_dup.release())) {
      throw EnvoyException(absl::StrCat("Failed to load trusted client CA certificates from ",
                                        config_->caCertPath()));
    }
  }

  // Check for EOF.
  const uint32_t err = ERR_peek_last_error();
  if (ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
    ERR_clear_error();
  } else {
    throw EnvoyException(
        absl::StrCat("Failed to load trusted client CA certificates from ", config_->caCertPath()));
  }
  SSL_CTX_set_client_CA_list(ctx, list.release());

  if (require_client_cert) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  }
  // Set the verify_depth
  if (config_->maxVerifyDepth().has_value()) {
    SSL_CTX_set_verify_depth(ctx, config_->maxVerifyDepth().value());
  }
}

Envoy::Ssl::CertificateDetailsPtr DefaultCertValidator::getCaCertInformation() const {
  if (ca_cert_ == nullptr) {
    return nullptr;
  }
  return Utility::certificateDetails(ca_cert_.get(), getCaFileName(), time_source_);
}

absl::optional<uint32_t> DefaultCertValidator::daysUntilFirstCertExpires() const {
  return Utility::getDaysUntilExpiration(ca_cert_.get(), time_source_);
}

class DefaultCertValidatorFactory : public CertValidatorFactory {
public:
  CertValidatorPtr createCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                                       SslStats& stats, TimeSource& time_source) override {
    return std::make_unique<DefaultCertValidator>(config, stats, time_source);
  }

  std::string name() const override { return "envoy.tls.cert_validator.default"; }
};

REGISTER_FACTORY(DefaultCertValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

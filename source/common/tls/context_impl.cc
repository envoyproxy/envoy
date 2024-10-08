#include "source/common/tls/context_impl.h"

#include <openssl/ssl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/stats/scope.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/utility.h"
#include "source/common/tls/cert_validator/factory.h"
#include "source/common/tls/stats.h"
#include "source/common/tls/utility.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "cert_validator/cert_validator.h"
#include "openssl/evp.h"
#include "openssl/hmac.h"
#include "openssl/pkcs12.h"
#include "openssl/rand.h"

namespace Envoy {
namespace {

void logSslErrorChain() {
  while (uint64_t err = ERR_get_error()) {
    ENVOY_LOG_MISC(debug, "SSL error: {}:{}:{}:{}", err,
                   absl::NullSafeStringView(ERR_lib_error_string(err)),
                   absl::NullSafeStringView(ERR_func_error_string(err)), ERR_GET_REASON(err),
                   absl::NullSafeStringView(ERR_reason_error_string(err)));
  }
}

} // namespace

namespace Extensions {
namespace TransportSockets {
namespace Tls {

int ContextImpl::sslExtendedSocketInfoIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int ssl_context_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(ssl_context_index >= 0, "");
    return ssl_context_index;
  }());
}

ContextImpl::ContextImpl(Stats::Scope& scope, const Envoy::Ssl::ContextConfig& config,
                         Server::Configuration::CommonFactoryContext& factory_context,
                         Ssl::ContextAdditionalInitFunc additional_init,
                         absl::Status& creation_status)
    : scope_(scope), stats_(generateSslStats(scope)), factory_context_(factory_context),
      tls_max_version_(config.maxProtocolVersion()),
      stat_name_set_(scope.symbolTable().makeSet("TransportSockets::Tls")),
      unknown_ssl_cipher_(stat_name_set_->add("unknown_ssl_cipher")),
      unknown_ssl_curve_(stat_name_set_->add("unknown_ssl_curve")),
      unknown_ssl_algorithm_(stat_name_set_->add("unknown_ssl_algorithm")),
      unknown_ssl_version_(stat_name_set_->add("unknown_ssl_version")),
      ssl_ciphers_(stat_name_set_->add("ssl.ciphers")),
      ssl_versions_(stat_name_set_->add("ssl.versions")),
      ssl_curves_(stat_name_set_->add("ssl.curves")),
      ssl_sigalgs_(stat_name_set_->add("ssl.sigalgs")), capabilities_(config.capabilities()),
      tls_keylog_local_(config.tlsKeyLogLocal()), tls_keylog_remote_(config.tlsKeyLogRemote()) {

  auto cert_validator_name = getCertValidatorName(config.certificateValidationContext());
  auto cert_validator_factory =
      Registry::FactoryRegistry<CertValidatorFactory>::getFactory(cert_validator_name);

  if (!cert_validator_factory) {
    creation_status = absl::InvalidArgumentError(
        absl::StrCat("Failed to get certificate validator factory for ", cert_validator_name));
    return;
  }

  cert_validator_ = cert_validator_factory->createCertValidator(
      config.certificateValidationContext(), stats_, factory_context_);

  const auto tls_certificates = config.tlsCertificates();
  tls_contexts_.resize(std::max(static_cast<size_t>(1), tls_certificates.size()));

  std::vector<SSL_CTX*> ssl_contexts(tls_contexts_.size());
  for (size_t i = 0; i < tls_contexts_.size(); i++) {
    auto& ctx = tls_contexts_[i];
    ctx.ssl_ctx_.reset(SSL_CTX_new(TLS_method()));
    ssl_contexts[i] = ctx.ssl_ctx_.get();

    int rc = SSL_CTX_set_app_data(ctx.ssl_ctx_.get(), this);
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

    rc = SSL_CTX_set_min_proto_version(ctx.ssl_ctx_.get(), config.minProtocolVersion());
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

    rc = SSL_CTX_set_max_proto_version(ctx.ssl_ctx_.get(), config.maxProtocolVersion());
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

    if (!capabilities_.provides_ciphers_and_curves &&
        !SSL_CTX_set_strict_cipher_list(ctx.ssl_ctx_.get(), config.cipherSuites().c_str())) {
      // Break up a set of ciphers into each individual cipher and try them each individually in
      // order to attempt to log which specific one failed. Example of config.cipherSuites():
      // "-ALL:[ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]:ECDHE-ECDSA-AES128-SHA".
      //
      // "-" is both an operator when in the leading position of a token (-ALL: don't allow this
      // cipher), and the common separator in names (ECDHE-ECDSA-AES128-GCM-SHA256). Don't split on
      // it because it will separate pieces of the same cipher. When it is a leading character, it
      // is removed below.
      std::vector<absl::string_view> ciphers =
          StringUtil::splitToken(config.cipherSuites(), ":+![|]", false);
      std::vector<std::string> bad_ciphers;
      for (const auto& cipher : ciphers) {
        std::string cipher_str(cipher);

        if (absl::StartsWith(cipher_str, "-")) {
          cipher_str.erase(cipher_str.begin());
        }

        if (!SSL_CTX_set_strict_cipher_list(ctx.ssl_ctx_.get(), cipher_str.c_str())) {
          bad_ciphers.push_back(cipher_str);
        }
      }
      creation_status = absl::InvalidArgumentError(
          fmt::format("Failed to initialize cipher suites {}. The following "
                      "ciphers were rejected when tried individually: {}",
                      config.cipherSuites(), absl::StrJoin(bad_ciphers, ", ")));
      return;
    }

    if (!capabilities_.provides_ciphers_and_curves &&
        !SSL_CTX_set1_curves_list(ctx.ssl_ctx_.get(), config.ecdhCurves().c_str())) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Failed to initialize ECDH curves ", config.ecdhCurves()));
      return;
    }

    // Set signature algorithms if given, otherwise fall back to BoringSSL defaults.
    if (!capabilities_.provides_sigalgs && !config.signatureAlgorithms().empty()) {
      if (!SSL_CTX_set1_sigalgs_list(ctx.ssl_ctx_.get(), config.signatureAlgorithms().c_str())) {
        creation_status = absl::InvalidArgumentError(absl::StrCat(
            "Failed to initialize TLS signature algorithms ", config.signatureAlgorithms()));
        return;
      }
    }
  }

  auto verify_mode_or_error = cert_validator_->initializeSslContexts(
      ssl_contexts, config.capabilities().provides_certificates);
  SET_AND_RETURN_IF_NOT_OK(verify_mode_or_error.status(), creation_status);
  auto verify_mode = verify_mode_or_error.value();

  if (!capabilities_.verifies_peer_certificates) {
    for (auto ctx : ssl_contexts) {
      if (verify_mode != SSL_VERIFY_NONE) {
        // TODO(danzh) Envoy's use of SSL_VERIFY_NONE does not quite match the actual semantics as
        // a client. As a client, SSL_VERIFY_NONE means to verify the certificate (which will fail
        // without trust anchors), save the result in the session ticket, but otherwise continue
        // with the handshake. But Envoy actually wants it to accept all certificates. The
        // disadvantage of using SSL_VERIFY_NONE is that it records the verify_result, which Envoy
        // never queries but gets saved in session tickets, and tries to find an anchor that isn't
        // there. And also it differs from server side behavior of SSL_VERIFY_NONE which won't
        // even request client certs. So, instead, we should configure a callback to skip
        // validation and always supply the callback to boring SSL.
        SSL_CTX_set_custom_verify(ctx, verify_mode, customVerifyCallback);
        SSL_CTX_set_reverify_on_resume(ctx, /*reverify_on_resume_enabled)=*/1);
      }
    }
  }

#ifdef BORINGSSL_FIPS
  if (!capabilities_.is_fips_compliant) {
    creation_status = absl::InvalidArgumentError(
        "Can't load a FIPS noncompliant custom handshaker while running in FIPS compliant mode.");
    return;
  }
#endif

  if (!capabilities_.provides_certificates) {
    for (uint32_t i = 0; i < tls_certificates.size(); ++i) {
      auto& ctx = tls_contexts_[i];
      // Load certificate chain.
      const auto& tls_certificate = tls_certificates[i].get();
      if (!tls_certificate.pkcs12().empty()) {
        creation_status = ctx.loadPkcs12(tls_certificate.pkcs12(), tls_certificate.pkcs12Path(),
                                         tls_certificate.password());
      } else {
        creation_status = ctx.loadCertificateChain(tls_certificate.certificateChain(),
                                                   tls_certificate.certificateChainPath());
      }
      if (!creation_status.ok()) {
        return;
      }
      // The must staple extension means the certificate promises to carry
      // with it an OCSP staple. https://tools.ietf.org/html/rfc7633#section-6
      constexpr absl::string_view tls_feature_ext = "1.3.6.1.5.5.7.1.24";
      constexpr absl::string_view must_staple_ext_value = "\x30\x3\x02\x01\x05";
      auto must_staple = Utility::getCertificateExtensionValue(*ctx.cert_chain_, tls_feature_ext);
      if (must_staple == must_staple_ext_value) {
        ctx.is_must_staple_ = true;
      }

      bssl::UniquePtr<EVP_PKEY> public_key(X509_get_pubkey(ctx.cert_chain_.get()));
      const int pkey_id = EVP_PKEY_id(public_key.get());
      switch (pkey_id) {
      case EVP_PKEY_EC: {
        // We only support P-256, P-384 or P-521 ECDSA today.
        const EC_KEY* ecdsa_public_key = EVP_PKEY_get0_EC_KEY(public_key.get());
        // Since we checked the key type above, this should be valid.
        ASSERT(ecdsa_public_key != nullptr);
        const EC_GROUP* ecdsa_group = EC_KEY_get0_group(ecdsa_public_key);
        const int ec_group_curve_name = EC_GROUP_get_curve_name(ecdsa_group);
        if (ecdsa_group == nullptr ||
            (ec_group_curve_name != NID_X9_62_prime256v1 && ec_group_curve_name != NID_secp384r1 &&
             ec_group_curve_name != NID_secp521r1)) {
          creation_status = absl::InvalidArgumentError(
              fmt::format("Failed to load certificate chain from {}, only P-256, "
                          "P-384 or P-521 ECDSA certificates are supported",
                          ctx.cert_chain_file_path_));
          return;
        }
        ctx.ec_group_curve_name_ = ec_group_curve_name;
      } break;
      case EVP_PKEY_RSA: {
        // We require RSA certificates with 2048-bit or larger keys.
        const RSA* rsa_public_key = EVP_PKEY_get0_RSA(public_key.get());
        // Since we checked the key type above, this should be valid.
        ASSERT(rsa_public_key != nullptr);
        const unsigned rsa_key_length = RSA_bits(rsa_public_key);
#ifdef BORINGSSL_FIPS
        if (rsa_key_length != 2048 && rsa_key_length != 3072 && rsa_key_length != 4096) {
          creation_status = absl::InvalidArgumentError(
              fmt::format("Failed to load certificate chain from {}, only RSA certificates with "
                          "2048-bit, 3072-bit or 4096-bit keys are supported in FIPS mode",
                          ctx.cert_chain_file_path_));
          return;
        }
#else
        if (rsa_key_length < 2048) {
          creation_status = absl::InvalidArgumentError(
              fmt::format("Failed to load certificate chain from {}, only RSA "
                          "certificates with 2048-bit or larger keys are supported",
                          ctx.cert_chain_file_path_));
          return;
        }
#endif
      } break;
#ifdef BORINGSSL_FIPS
      default:
        creation_status = absl::InvalidArgumentError(
            fmt::format("Failed to load certificate chain from {}, only RSA and "
                        "ECDSA certificates are supported in FIPS mode",
                        ctx.cert_chain_file_path_));
        return;
#endif
      }

      Envoy::Ssl::PrivateKeyMethodProviderSharedPtr private_key_method_provider =
          tls_certificate.privateKeyMethod();
      // We either have a private key or a BoringSSL private key method provider.
      if (private_key_method_provider) {
        ctx.private_key_method_provider_ = private_key_method_provider;
        // The provider has a reference to the private key method for the context lifetime.
        Ssl::BoringSslPrivateKeyMethodSharedPtr private_key_method =
            private_key_method_provider->getBoringSslPrivateKeyMethod();
        if (private_key_method == nullptr) {
          creation_status = absl::InvalidArgumentError(
              fmt::format("Failed to get BoringSSL private key method from provider"));
          return;
        }
#ifdef BORINGSSL_FIPS
        if (!ctx.private_key_method_provider_->checkFips()) {
          creation_status = absl::InvalidArgumentError(
              fmt::format("Private key method doesn't support FIPS mode with current parameters"));
          return;
        }
#endif
        SSL_CTX_set_private_key_method(ctx.ssl_ctx_.get(), private_key_method.get());
      } else if (!tls_certificate.privateKey().empty()) {
        // Load private key.
        creation_status =
            ctx.loadPrivateKey(tls_certificate.privateKey(), tls_certificate.privateKeyPath(),
                               tls_certificate.password());
        if (!creation_status.ok()) {
          return;
        }
      }

      if (additional_init != nullptr) {
        absl::Status init_status = additional_init(ctx, tls_certificate);
        SET_AND_RETURN_IF_NOT_OK(creation_status, init_status);
      }
    }
  }

  parsed_alpn_protocols_ = parseAlpnProtocols(config.alpnProtocols(), creation_status);
  SET_AND_RETURN_IF_NOT_OK(creation_status, creation_status);

#if BORINGSSL_API_VERSION >= 21
  // Register stat names based on lists reported by BoringSSL.
  std::vector<const char*> list(SSL_get_all_cipher_names(nullptr, 0));
  SSL_get_all_cipher_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);

  list.resize(SSL_get_all_curve_names(nullptr, 0));
  SSL_get_all_curve_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);

  list.resize(SSL_get_all_signature_algorithm_names(nullptr, 0));
  SSL_get_all_signature_algorithm_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);

  list.resize(SSL_get_all_version_names(nullptr, 0));
  SSL_get_all_version_names(list.data(), list.size());
  stat_name_set_->rememberBuiltins(list);
#else
  // Use the SSL library to iterate over the configured ciphers.
  //
  // Note that if a negotiated cipher suite is outside of this set, we'll issue an ENVOY_BUG.
  for (Ssl::TlsContext& tls_context : tls_contexts_) {
    for (const SSL_CIPHER* cipher : SSL_CTX_get_ciphers(tls_context.ssl_ctx_.get())) {
      stat_name_set_->rememberBuiltin(SSL_CIPHER_get_name(cipher));
    }
  }

  // Add supported cipher suites from the TLS 1.3 spec:
  // https://tools.ietf.org/html/rfc8446#appendix-B.4
  // AES-CCM cipher suites are removed (no BoringSSL support).
  //
  // Note that if a negotiated cipher suite is outside of this set, we'll issue an ENVOY_BUG.
  stat_name_set_->rememberBuiltins(
      {"TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384", "TLS_CHACHA20_POLY1305_SHA256"});

  // All supported curves. Source:
  // https://github.com/google/boringssl/blob/3743aafdacff2f7b083615a043a37101f740fa53/ssl/ssl_key_share.cc#L302-L309
  //
  // Note that if a negotiated curve is outside of this set, we'll issue an ENVOY_BUG.
  stat_name_set_->rememberBuiltins({"P-224", "P-256", "P-384", "P-521", "X25519", "CECPQ2"});

  // All supported signature algorithms. Source:
  // https://github.com/google/boringssl/blob/3743aafdacff2f7b083615a043a37101f740fa53/ssl/ssl_privkey.cc#L436-L453
  //
  // Note that if a negotiated algorithm is outside of this set, we'll issue an ENVOY_BUG.
  stat_name_set_->rememberBuiltins({
      "rsa_pkcs1_md5_sha1",
      "rsa_pkcs1_sha1",
      "rsa_pkcs1_sha256",
      "rsa_pkcs1_sha384",
      "rsa_pkcs1_sha512",
      "ecdsa_sha1",
      "ecdsa_secp256r1_sha256",
      "ecdsa_secp384r1_sha384",
      "ecdsa_secp521r1_sha512",
      "rsa_pss_rsae_sha256",
      "rsa_pss_rsae_sha384",
      "rsa_pss_rsae_sha512",
      "ed25519",
  });

  // All supported protocol versions.
  //
  // Note that if a negotiated version is outside of this set, we'll issue an ENVOY_BUG.
  stat_name_set_->rememberBuiltins({"TLSv1", "TLSv1.1", "TLSv1.2", "TLSv1.3"});
#endif

  // As late as possible, run the custom SSL_CTX configuration callback on each
  // SSL_CTX, if set.
  if (auto sslctx_cb = config.sslctxCb(); sslctx_cb) {
    for (Ssl::TlsContext& ctx : tls_contexts_) {
      sslctx_cb(ctx.ssl_ctx_.get());
    }
  }

  if (!config.tlsKeyLogPath().empty()) {
    ENVOY_LOG(debug, "Enable tls key log");
    auto file_or_error = config.accessLogManager().createAccessLog(
        Filesystem::FilePathAndType{Filesystem::DestinationType::File, config.tlsKeyLogPath()});
    SET_AND_RETURN_IF_NOT_OK(file_or_error.status(), creation_status);
    tls_keylog_file_ = file_or_error.value();
    for (auto& context : tls_contexts_) {
      SSL_CTX* ctx = context.ssl_ctx_.get();
      ASSERT(ctx != nullptr);
      SSL_CTX_set_keylog_callback(ctx, keylogCallback);
    }
  }
}

void ContextImpl::keylogCallback(const SSL* ssl, const char* line) {
  ASSERT(ssl != nullptr);
  auto callbacks =
      static_cast<Network::TransportSocketCallbacks*>(SSL_get_ex_data(ssl, sslSocketIndex()));
  auto ctx = static_cast<ContextImpl*>(SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl)));
  ASSERT(callbacks != nullptr);
  ASSERT(ctx != nullptr);

  if ((ctx->tls_keylog_local_.getIpListSize() == 0 ||
       ctx->tls_keylog_local_.contains(
           *(callbacks->connection().connectionInfoProvider().localAddress()))) &&
      (ctx->tls_keylog_remote_.getIpListSize() == 0 ||
       ctx->tls_keylog_remote_.contains(
           *(callbacks->connection().connectionInfoProvider().remoteAddress())))) {
    ctx->tls_keylog_file_->write(absl::StrCat(line, "\n"));
  }
}

int ContextImpl::sslSocketIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int ssl_socket_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(ssl_socket_index >= 0, "");
    return ssl_socket_index;
  }());
}

std::vector<uint8_t> ContextImpl::parseAlpnProtocols(const std::string& alpn_protocols,
                                                     absl::Status& parse_status) {
  if (alpn_protocols.empty()) {
    return {};
  }

  if (alpn_protocols.size() >= 65535) {
    parse_status = absl::InvalidArgumentError("Invalid ALPN protocol string");
    return {};
  }

  std::vector<uint8_t> out(alpn_protocols.size() + 1);
  size_t start = 0;
  for (size_t i = 0; i <= alpn_protocols.size(); i++) {
    if (i == alpn_protocols.size() || alpn_protocols[i] == ',') {
      if (i - start > 255) {
        parse_status = absl::InvalidArgumentError("Invalid ALPN protocol string");
        return {};
      }

      out[start] = i - start;
      start = i + 1;
    } else {
      out[i + 1] = alpn_protocols[i];
    }
  }

  return out;
}

absl::StatusOr<bssl::UniquePtr<SSL>>
ContextImpl::newSsl(const Network::TransportSocketOptionsConstSharedPtr& options) {
  // We use the first certificate for a new SSL object, later in the
  // SSL_CTX_set_select_certificate_cb() callback following ClientHello, we replace with the
  // selected certificate via SSL_set_SSL_CTX().
  auto ssl_con = bssl::UniquePtr<SSL>(SSL_new(tls_contexts_[0].ssl_ctx_.get()));
  SSL_set_app_data(ssl_con.get(), &options);
  return ssl_con;
}

enum ssl_verify_result_t ContextImpl::customVerifyCallback(SSL* ssl, uint8_t* out_alert) {
  auto* extended_socket_info = reinterpret_cast<Envoy::Ssl::SslExtendedSocketInfo*>(
      SSL_get_ex_data(ssl, ContextImpl::sslExtendedSocketInfoIndex()));
  if (extended_socket_info->certificateValidationResult() != Ssl::ValidateStatus::NotStarted) {
    if (extended_socket_info->certificateValidationResult() == Ssl::ValidateStatus::Pending) {
      return ssl_verify_retry;
    }
    ENVOY_LOG(trace, "Already has a result: {}",
              static_cast<int>(extended_socket_info->certificateValidationStatus()));
    // Already has a binary result, return immediately.
    *out_alert = extended_socket_info->certificateValidationAlert();
    return extended_socket_info->certificateValidationResult() == Ssl::ValidateStatus::Successful
               ? ssl_verify_ok
               : ssl_verify_invalid;
  }
  // Hasn't kicked off any validation for this connection yet.
  SSL_CTX* ssl_ctx = SSL_get_SSL_CTX(ssl);
  ContextImpl* context_impl = static_cast<ContextImpl*>(SSL_CTX_get_app_data(ssl_ctx));
  auto transport_socket_options_shared_ptr_ptr =
      static_cast<const Network::TransportSocketOptionsConstSharedPtr*>(SSL_get_app_data(ssl));
  ASSERT(transport_socket_options_shared_ptr_ptr);
  ValidationResults result = context_impl->customVerifyCertChain(
      extended_socket_info, *transport_socket_options_shared_ptr_ptr, ssl);
  switch (result.status) {
  case ValidationResults::ValidationStatus::Successful:
    return ssl_verify_ok;
  case ValidationResults::ValidationStatus::Pending:
    return ssl_verify_retry;
  case ValidationResults::ValidationStatus::Failed: {
    if (result.tls_alert.has_value() && out_alert) {
      *out_alert = result.tls_alert.value();
    }
    return ssl_verify_invalid;
  }
  }
  PANIC("not reached");
}

ValidationResults ContextImpl::customVerifyCertChain(
    Envoy::Ssl::SslExtendedSocketInfo* extended_socket_info,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options, SSL* ssl) {
  ASSERT(extended_socket_info);
  STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl);
  if (cert_chain == nullptr) {
    extended_socket_info->setCertificateValidationStatus(Ssl::ClientValidationStatus::NotValidated);
    stats_.fail_verify_error_.inc();
    ENVOY_LOG(debug, "verify cert failed: no cert chain");
    return {ValidationResults::ValidationStatus::Failed, Ssl::ClientValidationStatus::NotValidated,
            SSL_AD_INTERNAL_ERROR, absl::nullopt};
  }
  ASSERT(cert_validator_);
  const char* host_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

  CertValidator::ExtraValidationContext validation_ctx;
  validation_ctx.callbacks =
      static_cast<Network::TransportSocketCallbacks*>(SSL_get_ex_data(ssl, sslSocketIndex()));

  ValidationResults result = cert_validator_->doVerifyCertChain(
      *cert_chain, extended_socket_info->createValidateResultCallback(), transport_socket_options,
      *SSL_get_SSL_CTX(ssl), validation_ctx, SSL_is_server(ssl),
      absl::NullSafeStringView(host_name));
  if (result.status != ValidationResults::ValidationStatus::Pending) {
    extended_socket_info->setCertificateValidationStatus(result.detailed_status);
    extended_socket_info->onCertificateValidationCompleted(
        result.status == ValidationResults::ValidationStatus::Successful, false);
  }
  return result;
}

void ContextImpl::incCounter(const Stats::StatName name, absl::string_view value,
                             const Stats::StatName fallback) const {
  const Stats::StatName value_stat_name = stat_name_set_->getBuiltin(value, fallback);
  ENVOY_BUG(value_stat_name != fallback,
            absl::StrCat("Unexpected ", scope_.symbolTable().toString(name), " value: ", value));
  Stats::Utility::counterFromElements(scope_, {name, value_stat_name}).inc();
}

void ContextImpl::logHandshake(SSL* ssl) const {
  stats_.handshake_.inc();

  if (SSL_session_reused(ssl)) {
    stats_.session_reused_.inc();
  }

  incCounter(ssl_ciphers_, SSL_get_cipher_name(ssl), unknown_ssl_cipher_);
  incCounter(ssl_versions_, SSL_get_version(ssl), unknown_ssl_version_);

  const uint16_t curve_id = SSL_get_curve_id(ssl);
  if (curve_id) {
    incCounter(ssl_curves_, SSL_get_curve_name(curve_id), unknown_ssl_curve_);
  }

  const uint16_t sigalg_id = SSL_get_peer_signature_algorithm(ssl);
  if (sigalg_id) {
    const char* sigalg = SSL_get_signature_algorithm_name(sigalg_id, 1 /* include curve */);
    incCounter(ssl_sigalgs_, sigalg, unknown_ssl_algorithm_);
  }

  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
  if (!cert.get()) {
    stats_.no_certificate_.inc();
  }

#if defined(BORINGSSL_FIPS) && BORINGSSL_API_VERSION >= 18
#error "Delete preprocessor check below; no longer needed"
#endif

#if BORINGSSL_API_VERSION >= 18
  // Increment the `was_key_usage_invalid_` stats to indicate the given cert would have triggered an
  // error but is allowed because the enforcement that rsa key usage and tls usage need to be
  // matched has been disabled.
  if (SSL_was_key_usage_invalid(ssl)) {
    stats_.was_key_usage_invalid_.inc();
  }
#endif // BORINGSSL_API_VERSION
}

std::vector<Ssl::PrivateKeyMethodProviderSharedPtr> ContextImpl::getPrivateKeyMethodProviders() {
  std::vector<Envoy::Ssl::PrivateKeyMethodProviderSharedPtr> providers;

  for (auto& tls_context : tls_contexts_) {
    Envoy::Ssl::PrivateKeyMethodProviderSharedPtr provider =
        tls_context.getPrivateKeyMethodProvider();
    if (provider) {
      providers.push_back(provider);
    }
  }
  return providers;
}

absl::optional<uint32_t> ContextImpl::daysUntilFirstCertExpires() const {
  absl::optional<uint32_t> daysUntilExpiration = cert_validator_->daysUntilFirstCertExpires();
  if (!daysUntilExpiration.has_value()) {
    return absl::nullopt;
  }
  for (auto& ctx : tls_contexts_) {
    const absl::optional<uint32_t> tmp =
        Utility::getDaysUntilExpiration(ctx.cert_chain_.get(), factory_context_.timeSource());
    if (!tmp.has_value()) {
      return absl::nullopt;
    }
    daysUntilExpiration = std::min<uint32_t>(tmp.value(), daysUntilExpiration.value());
  }
  return daysUntilExpiration;
}

absl::optional<uint64_t> ContextImpl::secondsUntilFirstOcspResponseExpires() const {
  absl::optional<uint64_t> secs_until_expiration;
  for (auto& ctx : tls_contexts_) {
    if (ctx.ocsp_response_) {
      uint64_t next_expiration = ctx.ocsp_response_->secondsUntilExpiration();
      secs_until_expiration = std::min<uint64_t>(
          next_expiration, secs_until_expiration.value_or(std::numeric_limits<uint64_t>::max()));
    }
  }

  return secs_until_expiration;
}

Envoy::Ssl::CertificateDetailsPtr ContextImpl::getCaCertInformation() const {
  return cert_validator_->getCaCertInformation();
}

std::vector<Envoy::Ssl::CertificateDetailsPtr> ContextImpl::getCertChainInformation() const {
  std::vector<Envoy::Ssl::CertificateDetailsPtr> cert_details;
  for (const auto& ctx : tls_contexts_) {
    if (ctx.cert_chain_ == nullptr) {
      continue;
    }

    auto detail = Utility::certificateDetails(ctx.cert_chain_.get(), ctx.getCertChainFileName(),
                                              factory_context_.timeSource());
    auto ocsp_resp = ctx.ocsp_response_.get();
    if (ocsp_resp) {
      auto* ocsp_details = detail->mutable_ocsp_details();
      ProtobufWkt::Timestamp* valid_from = ocsp_details->mutable_valid_from();
      TimestampUtil::systemClockToTimestamp(ocsp_resp->getThisUpdate(), *valid_from);
      ProtobufWkt::Timestamp* expiration = ocsp_details->mutable_expiration();
      TimestampUtil::systemClockToTimestamp(ocsp_resp->getNextUpdate(), *expiration);
    }
    cert_details.push_back(std::move(detail));
  }
  return cert_details;
}

bool ContextImpl::parseAndSetAlpn(const std::vector<std::string>& alpn, SSL& ssl,
                                  absl::Status& parse_status) {
  std::vector<uint8_t> parsed_alpn = parseAlpnProtocols(absl::StrJoin(alpn, ","), parse_status);
  if (!parse_status.ok()) {
    return false;
  }
  if (!parsed_alpn.empty()) {
    const int rc = SSL_set_alpn_protos(&ssl, parsed_alpn.data(), parsed_alpn.size());
    // This should only if memory allocation fails, e.g. OOM.
    RELEASE_ASSERT(rc == 0, Utility::getLastCryptoError().value_or(""));
    return true;
  }

  return false;
}

ValidationResults ContextImpl::customVerifyCertChainForQuic(
    STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback, bool is_server,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    const CertValidator::ExtraValidationContext& validation_context, const std::string& host_name) {
  ASSERT(!tls_contexts_.empty());
  // It doesn't matter which SSL context is used, because they share the same cert validation
  // config.
  SSL_CTX* ssl_ctx = tls_contexts_[0].ssl_ctx_.get();
  if (SSL_CTX_get_verify_mode(ssl_ctx) == SSL_VERIFY_NONE) {
    // Skip validation if the TLS is configured SSL_VERIFY_NONE.
    return {ValidationResults::ValidationStatus::Successful,
            Envoy::Ssl::ClientValidationStatus::NotValidated, absl::nullopt, absl::nullopt};
  }
  ValidationResults result =
      cert_validator_->doVerifyCertChain(cert_chain, std::move(callback), transport_socket_options,
                                         *ssl_ctx, validation_context, is_server, host_name);
  return result;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions

namespace Ssl {

bool TlsContext::isCipherEnabled(uint16_t cipher_id, uint16_t client_version) const {
  const SSL_CIPHER* c = SSL_get_cipher_by_value(cipher_id);
  if (c == nullptr) {
    return false;
  }
  // Skip TLS 1.2 only ciphersuites unless the client supports it.
  if (SSL_CIPHER_get_min_version(c) > client_version) {
    return false;
  }
  if (SSL_CIPHER_get_auth_nid(c) != NID_auth_ecdsa) {
    return false;
  }
  for (const SSL_CIPHER* our_c : SSL_CTX_get_ciphers(ssl_ctx_.get())) {
    if (SSL_CIPHER_get_id(our_c) == SSL_CIPHER_get_id(c)) {
      return true;
    }
  }
  return false;
}

absl::Status TlsContext::loadCertificateChain(const std::string& data,
                                              const std::string& data_path) {
  cert_chain_file_path_ = data_path;
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(data.data()), data.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  cert_chain_.reset(PEM_read_bio_X509_AUX(bio.get(), nullptr, nullptr, nullptr));
  if (cert_chain_ == nullptr || !SSL_CTX_use_certificate(ssl_ctx_.get(), cert_chain_.get())) {
    logSslErrorChain();
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to load certificate chain from ", cert_chain_file_path_));
  }
  // Read rest of the certificate chain.
  while (true) {
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (cert == nullptr) {
      break;
    }
    if (!SSL_CTX_add_extra_chain_cert(ssl_ctx_.get(), cert.get())) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to load certificate chain from ", cert_chain_file_path_));
    }
    // SSL_CTX_add_extra_chain_cert() takes ownership.
    cert.release();
  }
  // Check for EOF.
  const uint32_t err = ERR_peek_last_error();
  if (ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
    ERR_clear_error();
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to load certificate chain from ", cert_chain_file_path_));
  }
  return absl::OkStatus();
}

absl::Status TlsContext::loadPrivateKey(const std::string& data, const std::string& data_path,
                                        const std::string& password) {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(data.data()), data.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  bssl::UniquePtr<EVP_PKEY> pkey(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr,
                              !password.empty() ? const_cast<char*>(password.c_str()) : nullptr));

  if (pkey == nullptr || !SSL_CTX_use_PrivateKey(ssl_ctx_.get(), pkey.get())) {
    return absl::InvalidArgumentError(fmt::format(
        "Failed to load private key from {}, Cause: {}", data_path,
        Extensions::TransportSockets::Tls::Utility::getLastCryptoError().value_or("unknown")));
  }

  return checkPrivateKey(pkey, data_path);
}

absl::Status TlsContext::loadPkcs12(const std::string& data, const std::string& data_path,
                                    const std::string& password) {
  cert_chain_file_path_ = data_path;
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(data.data()), data.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  bssl::UniquePtr<PKCS12> pkcs12(d2i_PKCS12_bio(bio.get(), nullptr));

  EVP_PKEY* temp_private_key = nullptr;
  X509* temp_cert = nullptr;
  STACK_OF(X509)* temp_ca_certs = nullptr;
  if (pkcs12 == nullptr ||
      !PKCS12_parse(pkcs12.get(), !password.empty() ? const_cast<char*>(password.c_str()) : nullptr,
                    &temp_private_key, &temp_cert, &temp_ca_certs)) {
    logSslErrorChain();
    return absl::InvalidArgumentError(absl::StrCat("Failed to load pkcs12 from ", data_path));
  }
  cert_chain_.reset(temp_cert);
  bssl::UniquePtr<EVP_PKEY> pkey(temp_private_key);
  bssl::UniquePtr<STACK_OF(X509)> ca_certificates(temp_ca_certs);
  if (ca_certificates != nullptr) {
    X509* ca_cert = nullptr;
    while ((ca_cert = sk_X509_pop(ca_certificates.get())) != nullptr) {
      // This transfers ownership to ssl_ctx therefore ca_cert does not need to be freed.
      SSL_CTX_add_extra_chain_cert(ssl_ctx_.get(), ca_cert);
    }
  }
  if (!SSL_CTX_use_certificate(ssl_ctx_.get(), cert_chain_.get())) {
    logSslErrorChain();
    return absl::InvalidArgumentError(absl::StrCat("Failed to load certificate from ", data_path));
  }
  if (temp_private_key == nullptr || !SSL_CTX_use_PrivateKey(ssl_ctx_.get(), pkey.get())) {
    return absl::InvalidArgumentError(fmt::format(
        "Failed to load private key from {}, Cause: {}", data_path,
        Extensions::TransportSockets::Tls::Utility::getLastCryptoError().value_or("unknown")));
  }

  return checkPrivateKey(pkey, data_path);
}

absl::Status TlsContext::checkPrivateKey(const bssl::UniquePtr<EVP_PKEY>& pkey,
                                         const std::string& key_path) {
#ifdef BORINGSSL_FIPS
  // Verify that private keys are passing FIPS pairwise consistency tests.
  switch (EVP_PKEY_id(pkey.get())) {
  case EVP_PKEY_EC: {
    const EC_KEY* ecdsa_private_key = EVP_PKEY_get0_EC_KEY(pkey.get());
    if (!EC_KEY_check_fips(ecdsa_private_key)) {
      return absl::InvalidArgumentError(
          fmt::format("Failed to load private key from {}, ECDSA key failed "
                      "pairwise consistency test required in FIPS mode",
                      key_path));
    }
  } break;
  case EVP_PKEY_RSA: {
    RSA* rsa_private_key = EVP_PKEY_get0_RSA(pkey.get());
    if (!RSA_check_fips(rsa_private_key)) {
      return absl::InvalidArgumentError(
          fmt::format("Failed to load private key from {}, RSA key failed "
                      "pairwise consistency test required in FIPS mode",
                      key_path));
    }
  } break;
  }
#else
  UNREFERENCED_PARAMETER(pkey);
  UNREFERENCED_PARAMETER(key_path);
#endif
  return absl::OkStatus();
}

} // namespace Ssl
} // namespace Envoy

#include "source/extensions/transport_sockets/tls/context_impl.h"

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
#include "source/extensions/transport_sockets/tls/cert_validator/factory.h"
#include "source/extensions/transport_sockets/tls/stats.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "cert_validator/cert_validator.h"
#include "openssl/evp.h"
#include "openssl/hmac.h"
#include "openssl/pkcs12.h"
#include "openssl/rand.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

bool cbsContainsU16(CBS& cbs, uint16_t n) {
  while (CBS_len(&cbs) > 0) {
    uint16_t v;
    if (!CBS_get_u16(&cbs, &v)) {
      return false;
    }
    if (v == n) {
      return true;
    }
  }

  return false;
}

void logSslErrorChain() {
  while (uint64_t err = ERR_get_error()) {
    ENVOY_LOG_MISC(debug, "SSL error: {}:{}:{}:{}", err,
                   absl::NullSafeStringView(ERR_lib_error_string(err)),
                   absl::NullSafeStringView(ERR_func_error_string(err)), ERR_GET_REASON(err),
                   absl::NullSafeStringView(ERR_reason_error_string(err)));
  }
}

} // namespace

int ContextImpl::sslExtendedSocketInfoIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int ssl_context_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(ssl_context_index >= 0, "");
    return ssl_context_index;
  }());
}

ContextImpl::ContextImpl(Stats::Scope& scope, const Envoy::Ssl::ContextConfig& config,
                         TimeSource& time_source)
    : scope_(scope), stats_(generateSslStats(scope)), time_source_(time_source),
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
    throwEnvoyExceptionOrPanic(
        absl::StrCat("Failed to get certificate validator factory for ", cert_validator_name));
  }

  cert_validator_ = cert_validator_factory->createCertValidator(
      config.certificateValidationContext(), stats_, time_source_);

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
      throwEnvoyExceptionOrPanic(fmt::format("Failed to initialize cipher suites {}. The following "
                                             "ciphers were rejected when tried individually: {}",
                                             config.cipherSuites(),
                                             absl::StrJoin(bad_ciphers, ", ")));
    }

    if (!capabilities_.provides_ciphers_and_curves &&
        !SSL_CTX_set1_curves_list(ctx.ssl_ctx_.get(), config.ecdhCurves().c_str())) {
      throwEnvoyExceptionOrPanic(
          absl::StrCat("Failed to initialize ECDH curves ", config.ecdhCurves()));
    }

    // Set signature algorithms if given, otherwise fall back to BoringSSL defaults.
    if (!capabilities_.provides_sigalgs && !config.signatureAlgorithms().empty()) {
      if (!SSL_CTX_set1_sigalgs_list(ctx.ssl_ctx_.get(), config.signatureAlgorithms().c_str())) {
        throwEnvoyExceptionOrPanic(absl::StrCat("Failed to initialize TLS signature algorithms ",
                                                config.signatureAlgorithms()));
      }
    }
  }

  auto verify_mode = cert_validator_->initializeSslContexts(
      ssl_contexts, config.capabilities().provides_certificates);
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
    throwEnvoyExceptionOrPanic(
        "Can't load a FIPS noncompliant custom handshaker while running in FIPS compliant mode.");
  }
#endif

  if (!capabilities_.provides_certificates) {
    for (uint32_t i = 0; i < tls_certificates.size(); ++i) {
      auto& ctx = tls_contexts_[i];
      // Load certificate chain.
      const auto& tls_certificate = tls_certificates[i].get();
      if (!tls_certificate.pkcs12().empty()) {
        ctx.loadPkcs12(tls_certificate.pkcs12(), tls_certificate.pkcs12Path(),
                       tls_certificate.password());
      } else {
        ctx.loadCertificateChain(tls_certificate.certificateChain(),
                                 tls_certificate.certificateChainPath());
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
      ctx.is_ecdsa_ = pkey_id == EVP_PKEY_EC;
      switch (pkey_id) {
      case EVP_PKEY_EC: {
        // We only support P-256 ECDSA today.
        const EC_KEY* ecdsa_public_key = EVP_PKEY_get0_EC_KEY(public_key.get());
        // Since we checked the key type above, this should be valid.
        ASSERT(ecdsa_public_key != nullptr);
        const EC_GROUP* ecdsa_group = EC_KEY_get0_group(ecdsa_public_key);
        if (ecdsa_group == nullptr ||
            EC_GROUP_get_curve_name(ecdsa_group) != NID_X9_62_prime256v1) {
          throwEnvoyExceptionOrPanic(
              fmt::format("Failed to load certificate chain from {}, only P-256 "
                          "ECDSA certificates are supported",
                          ctx.cert_chain_file_path_));
        }
        ctx.is_ecdsa_ = true;
      } break;
      case EVP_PKEY_RSA: {
        // We require RSA certificates with 2048-bit or larger keys.
        const RSA* rsa_public_key = EVP_PKEY_get0_RSA(public_key.get());
        // Since we checked the key type above, this should be valid.
        ASSERT(rsa_public_key != nullptr);
        const unsigned rsa_key_length = RSA_bits(rsa_public_key);
#ifdef BORINGSSL_FIPS
        if (rsa_key_length != 2048 && rsa_key_length != 3072 && rsa_key_length != 4096) {
          throwEnvoyExceptionOrPanic(
              fmt::format("Failed to load certificate chain from {}, only RSA certificates with "
                          "2048-bit, 3072-bit or 4096-bit keys are supported in FIPS mode",
                          ctx.cert_chain_file_path_));
        }
#else
        if (rsa_key_length < 2048) {
          throwEnvoyExceptionOrPanic(
              fmt::format("Failed to load certificate chain from {}, only RSA "
                          "certificates with 2048-bit or larger keys are supported",
                          ctx.cert_chain_file_path_));
        }
#endif
      } break;
#ifdef BORINGSSL_FIPS
      default:
        throwEnvoyExceptionOrPanic(
            fmt::format("Failed to load certificate chain from {}, only RSA and "
                        "ECDSA certificates are supported in FIPS mode",
                        ctx.cert_chain_file_path_));
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
          throwEnvoyExceptionOrPanic(
              fmt::format("Failed to get BoringSSL private key method from provider"));
        }
#ifdef BORINGSSL_FIPS
        if (!ctx.private_key_method_provider_->checkFips()) {
          throwEnvoyExceptionOrPanic(
              fmt::format("Private key method doesn't support FIPS mode with current parameters"));
        }
#endif
        SSL_CTX_set_private_key_method(ctx.ssl_ctx_.get(), private_key_method.get());
      } else if (!tls_certificate.privateKey().empty()) {
        // Load private key.
        ctx.loadPrivateKey(tls_certificate.privateKey(), tls_certificate.privateKeyPath(),
                           tls_certificate.password());
      }
    }
  }

  // use the server's cipher list preferences
  for (auto& ctx : tls_contexts_) {
    SSL_CTX_set_options(ctx.ssl_ctx_.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);
  }

  parsed_alpn_protocols_ = parseAlpnProtocols(config.alpnProtocols());

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
  for (TlsContext& tls_context : tls_contexts_) {
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
    for (TlsContext& ctx : tls_contexts_) {
      sslctx_cb(ctx.ssl_ctx_.get());
    }
  }

  if (!config.tlsKeyLogPath().empty()) {
    ENVOY_LOG(debug, "Enable tls key log");
    tls_keylog_file_ = config.accessLogManager().createAccessLog(
        Filesystem::FilePathAndType{Filesystem::DestinationType::File, config.tlsKeyLogPath()});
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

int ServerContextImpl::alpnSelectCallback(const unsigned char** out, unsigned char* outlen,
                                          const unsigned char* in, unsigned int inlen) {
  // Currently this uses the standard selection algorithm in priority order.
  const uint8_t* alpn_data = parsed_alpn_protocols_.data();
  size_t alpn_data_size = parsed_alpn_protocols_.size();

  if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen, alpn_data, alpn_data_size, in,
                            inlen) != OPENSSL_NPN_NEGOTIATED) {
    return SSL_TLSEXT_ERR_NOACK;
  } else {
    return SSL_TLSEXT_ERR_OK;
  }
}

std::vector<uint8_t> ContextImpl::parseAlpnProtocols(const std::string& alpn_protocols) {
  if (alpn_protocols.empty()) {
    return {};
  }

  if (alpn_protocols.size() >= 65535) {
    throwEnvoyExceptionOrPanic("Invalid ALPN protocol string");
  }

  std::vector<uint8_t> out(alpn_protocols.size() + 1);
  size_t start = 0;
  for (size_t i = 0; i <= alpn_protocols.size(); i++) {
    if (i == alpn_protocols.size() || alpn_protocols[i] == ',') {
      if (i - start > 255) {
        throwEnvoyExceptionOrPanic("Invalid ALPN protocol string");
      }

      out[start] = i - start;
      start = i + 1;
    } else {
      out[i + 1] = alpn_protocols[i];
    }
  }

  return out;
}

bssl::UniquePtr<SSL>
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
        Utility::getDaysUntilExpiration(ctx.cert_chain_.get(), time_source_);
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
                                              time_source_);
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

ClientContextImpl::ClientContextImpl(Stats::Scope& scope,
                                     const Envoy::Ssl::ClientContextConfig& config,
                                     TimeSource& time_source)
    : ContextImpl(scope, config, time_source),
      server_name_indication_(config.serverNameIndication()),
      allow_renegotiation_(config.allowRenegotiation()),
      enforce_rsa_key_usage_(config.enforceRsaKeyUsage()),
      max_session_keys_(config.maxSessionKeys()) {
  // This should be guaranteed during configuration ingestion for client contexts.
  ASSERT(tls_contexts_.size() == 1);
  if (!parsed_alpn_protocols_.empty()) {
    for (auto& ctx : tls_contexts_) {
      const int rc = SSL_CTX_set_alpn_protos(ctx.ssl_ctx_.get(), parsed_alpn_protocols_.data(),
                                             parsed_alpn_protocols_.size());
      RELEASE_ASSERT(rc == 0, Utility::getLastCryptoError().value_or(""));
    }
  }

  if (max_session_keys_ > 0) {
    SSL_CTX_set_session_cache_mode(tls_contexts_[0].ssl_ctx_.get(), SSL_SESS_CACHE_CLIENT);
    SSL_CTX_sess_set_new_cb(
        tls_contexts_[0].ssl_ctx_.get(), [](SSL* ssl, SSL_SESSION* session) -> int {
          ContextImpl* context_impl =
              static_cast<ContextImpl*>(SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl)));
          ClientContextImpl* client_context_impl = dynamic_cast<ClientContextImpl*>(context_impl);
          RELEASE_ASSERT(client_context_impl != nullptr, ""); // for Coverity
          return client_context_impl->newSessionKey(session);
        });
  }
}

bool ContextImpl::parseAndSetAlpn(const std::vector<std::string>& alpn, SSL& ssl) {
  std::vector<uint8_t> parsed_alpn = parseAlpnProtocols(absl::StrJoin(alpn, ","));
  if (!parsed_alpn.empty()) {
    const int rc = SSL_set_alpn_protos(&ssl, parsed_alpn.data(), parsed_alpn.size());
    // This should only if memory allocation fails, e.g. OOM.
    RELEASE_ASSERT(rc == 0, Utility::getLastCryptoError().value_or(""));
    return true;
  }

  return false;
}

bssl::UniquePtr<SSL>
ClientContextImpl::newSsl(const Network::TransportSocketOptionsConstSharedPtr& options) {
  bssl::UniquePtr<SSL> ssl_con(ContextImpl::newSsl(options));

  const std::string server_name_indication = options && options->serverNameOverride().has_value()
                                                 ? options->serverNameOverride().value()
                                                 : server_name_indication_;
  if (!server_name_indication.empty()) {
    const int rc = SSL_set_tlsext_host_name(ssl_con.get(), server_name_indication.c_str());
    RELEASE_ASSERT(rc, Utility::getLastCryptoError().value_or(""));
  }

  if (options && !options->verifySubjectAltNameListOverride().empty()) {
    SSL_set_verify(ssl_con.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  }

  // We determine what ALPN using the following precedence:
  // 1. Option-provided ALPN override.
  // 2. ALPN statically configured in the upstream TLS context.
  // 3. Option-provided ALPN fallback.

  // At this point in the code the ALPN has already been set (if present) to the value specified in
  // the TLS context. We've stored this value in parsed_alpn_protocols_ so we can check that to see
  // if it's already been set.
  bool has_alpn_defined = !parsed_alpn_protocols_.empty();
  if (options) {
    // ALPN override takes precedence over TLS context specified, so blindly overwrite it.
    has_alpn_defined |= parseAndSetAlpn(options->applicationProtocolListOverride(), *ssl_con);
  }

  if (options && !has_alpn_defined && !options->applicationProtocolFallback().empty()) {
    // If ALPN hasn't already been set (either through TLS context or override), use the fallback.
    parseAndSetAlpn(options->applicationProtocolFallback(), *ssl_con);
  }

  if (allow_renegotiation_) {
    SSL_set_renegotiate_mode(ssl_con.get(), ssl_renegotiate_freely);
  }

  SSL_set_enforce_rsa_key_usage(ssl_con.get(), enforce_rsa_key_usage_);

  if (max_session_keys_ > 0) {
    if (session_keys_single_use_) {
      // Stored single-use session keys, use write/write locks.
      absl::WriterMutexLock l(&session_keys_mu_);
      if (!session_keys_.empty()) {
        // Use the most recently stored session key, since it has the highest
        // probability of still being recognized/accepted by the server.
        SSL_SESSION* session = session_keys_.front().get();
        SSL_set_session(ssl_con.get(), session);
        // Remove single-use session key (TLS 1.3) after first use.
        if (SSL_SESSION_should_be_single_use(session)) {
          session_keys_.pop_front();
        }
      }
    } else {
      // Never stored single-use session keys, use read/write locks.
      absl::ReaderMutexLock l(&session_keys_mu_);
      if (!session_keys_.empty()) {
        // Use the most recently stored session key, since it has the highest
        // probability of still being recognized/accepted by the server.
        SSL_SESSION* session = session_keys_.front().get();
        SSL_set_session(ssl_con.get(), session);
      }
    }
  }

  return ssl_con;
}

int ClientContextImpl::newSessionKey(SSL_SESSION* session) {
  // In case we ever store single-use session key (TLS 1.3),
  // we need to switch to using write/write locks.
  if (SSL_SESSION_should_be_single_use(session)) {
    session_keys_single_use_ = true;
  }
  absl::WriterMutexLock l(&session_keys_mu_);
  // Evict oldest entries.
  while (session_keys_.size() >= max_session_keys_) {
    session_keys_.pop_back();
  }
  // Add new session key at the front of the queue, so that it's used first.
  session_keys_.push_front(bssl::UniquePtr<SSL_SESSION>(session));
  return 1; // Tell BoringSSL that we took ownership of the session.
}

ServerContextImpl::ServerContextImpl(Stats::Scope& scope,
                                     const Envoy::Ssl::ServerContextConfig& config,
                                     const std::vector<std::string>& server_names,
                                     TimeSource& time_source)
    : ContextImpl(scope, config, time_source), session_ticket_keys_(config.sessionTicketKeys()),
      ocsp_staple_policy_(config.ocspStaplePolicy()),
      full_scan_certs_on_sni_mismatch_(config.fullScanCertsOnSNIMismatch()) {
  if (config.tlsCertificates().empty() && !config.capabilities().provides_certificates) {
    throwEnvoyExceptionOrPanic("Server TlsCertificates must have a certificate specified");
  }

  for (auto& ctx : tls_contexts_) {
    if (ctx.cert_chain_ == nullptr) {
      continue;
    }
    bssl::UniquePtr<EVP_PKEY> public_key(X509_get_pubkey(ctx.cert_chain_.get()));
    const int pkey_id = EVP_PKEY_id(public_key.get());
    // Load DNS SAN entries and Subject Common Name as server name patterns after certificate
    // chain loaded, and populate ServerNamesMap which will be used to match SNI.
    has_rsa_ |= (pkey_id == EVP_PKEY_RSA);
    populateServerNamesMap(ctx, pkey_id);
  }

  // Compute the session context ID hash. We use all the certificate identities,
  // since we should have a common ID for session resumption no matter what cert
  // is used. We do this early because it can throw an EnvoyException.
  const SessionContextID session_id = generateHashForSessionContextId(server_names);

  // First, configure the base context for ClientHello interception.
  // TODO(htuch): replace with SSL_IDENTITY when we have this as a means to do multi-cert in
  // BoringSSL.
  if (!config.capabilities().provides_certificates) {
    SSL_CTX_set_select_certificate_cb(
        tls_contexts_[0].ssl_ctx_.get(),
        [](const SSL_CLIENT_HELLO* client_hello) -> ssl_select_cert_result_t {
          return static_cast<ServerContextImpl*>(
                     SSL_CTX_get_app_data(SSL_get_SSL_CTX(client_hello->ssl)))
              ->selectTlsContext(client_hello);
        });
  }

  const auto tls_certificates = config.tlsCertificates();

  for (uint32_t i = 0; i < tls_certificates.size(); ++i) {
    auto& ctx = tls_contexts_[i];
    if (!config.capabilities().verifies_peer_certificates) {
      cert_validator_->addClientValidationContext(ctx.ssl_ctx_.get(),
                                                  config.requireClientCertificate());
    }

    if (!parsed_alpn_protocols_.empty() && !config.capabilities().handles_alpn_selection) {
      SSL_CTX_set_alpn_select_cb(
          ctx.ssl_ctx_.get(),
          [](SSL*, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
             unsigned int inlen, void* arg) -> int {
            return static_cast<ServerContextImpl*>(arg)->alpnSelectCallback(out, outlen, in, inlen);
          },
          this);
    }

    // If the handshaker handles session tickets natively, don't call
    // `SSL_CTX_set_tlsext_ticket_key_cb`.
    if (config.disableStatelessSessionResumption()) {
      SSL_CTX_set_options(ctx.ssl_ctx_.get(), SSL_OP_NO_TICKET);
    } else if (!session_ticket_keys_.empty() && !config.capabilities().handles_session_resumption) {
      SSL_CTX_set_tlsext_ticket_key_cb(
          ctx.ssl_ctx_.get(),
          [](SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx, HMAC_CTX* hmac_ctx,
             int encrypt) -> int {
            ContextImpl* context_impl =
                static_cast<ContextImpl*>(SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl)));
            ServerContextImpl* server_context_impl = dynamic_cast<ServerContextImpl*>(context_impl);
            RELEASE_ASSERT(server_context_impl != nullptr, ""); // for Coverity
            return server_context_impl->sessionTicketProcess(ssl, key_name, iv, ctx, hmac_ctx,
                                                             encrypt);
          });
    }

    if (config.disableStatefulSessionResumption()) {
      SSL_CTX_set_session_cache_mode(ctx.ssl_ctx_.get(), SSL_SESS_CACHE_OFF);
    }

    if (config.sessionTimeout() && !config.capabilities().handles_session_resumption) {
      auto timeout = config.sessionTimeout().value().count();
      SSL_CTX_set_timeout(ctx.ssl_ctx_.get(), uint32_t(timeout));
    }

    int rc =
        SSL_CTX_set_session_id_context(ctx.ssl_ctx_.get(), session_id.data(), session_id.size());
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

    auto& ocsp_resp_bytes = tls_certificates[i].get().ocspStaple();
    if (ocsp_resp_bytes.empty()) {
      if (ctx.is_must_staple_) {
        throwEnvoyExceptionOrPanic("OCSP response is required for must-staple certificate");
      }
      if (ocsp_staple_policy_ == Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple) {
        throwEnvoyExceptionOrPanic("Required OCSP response is missing from TLS context");
      }
    } else {
      auto response = std::make_unique<Ocsp::OcspResponseWrapper>(ocsp_resp_bytes, time_source_);
      if (!response->matchesCertificate(*ctx.cert_chain_)) {
        throwEnvoyExceptionOrPanic("OCSP response does not match its TLS certificate");
      }
      ctx.ocsp_response_ = std::move(response);
    }
  }
}

void ServerContextImpl::populateServerNamesMap(TlsContext& ctx, int pkey_id) {
  if (ctx.cert_chain_ == nullptr) {
    return;
  }

  auto populate = [&](const std::string& sn) {
    std::string sn_pattern = sn;
    if (absl::StartsWith(sn, "*.")) {
      sn_pattern = sn.substr(1);
    }
    PkeyTypesMap pkey_types_map;
    // Multiple certs with different key type are allowed for one server name pattern.
    auto sn_match = server_names_map_.try_emplace(sn_pattern, pkey_types_map).first;
    auto pt_match = sn_match->second.find(pkey_id);
    if (pt_match != sn_match->second.end()) {
      // When there are duplicate names, prefer the earlier one.
      //
      // If all of the SANs in a certificate are unused due to duplicates, it could be useful
      // to issue a warning, but that would require additional tracking that hasn't been
      // implemented.
      return;
    }
    sn_match->second.emplace(std::pair<int, std::reference_wrapper<TlsContext>>(pkey_id, ctx));
  };

  bssl::UniquePtr<GENERAL_NAMES> san_names(static_cast<GENERAL_NAMES*>(
      X509_get_ext_d2i(ctx.cert_chain_.get(), NID_subject_alt_name, nullptr, nullptr)));
  if (san_names != nullptr) {
    auto dns_sans = Utility::getSubjectAltNames(*ctx.cert_chain_, GEN_DNS);
    // https://www.rfc-editor.org/rfc/rfc6066#section-3
    // Currently, the only server names supported are DNS hostnames, so we
    // only save dns san entries to match SNI.
    for (const auto& san : dns_sans) {
      populate(san);
    }
  } else {
    // https://www.rfc-editor.org/rfc/rfc6125#section-6.4.4
    // As noted, a client MUST NOT seek a match for a reference identifier
    // of CN-ID if the presented identifiers include a DNS-ID, SRV-ID,
    // URI-ID, or any application-specific identifier types supported by the
    // client.
    X509_NAME* cert_subject = X509_get_subject_name(ctx.cert_chain_.get());
    const int cn_index = X509_NAME_get_index_by_NID(cert_subject, NID_commonName, -1);
    if (cn_index >= 0) {
      X509_NAME_ENTRY* cn_entry = X509_NAME_get_entry(cert_subject, cn_index);
      if (cn_entry) {
        ASN1_STRING* cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry);
        if (ASN1_STRING_length(cn_asn1) > 0) {
          std::string subject_cn(reinterpret_cast<const char*>(ASN1_STRING_data(cn_asn1)),
                                 ASN1_STRING_length(cn_asn1));
          populate(subject_cn);
        }
      }
    }
  }
}

ServerContextImpl::SessionContextID
ServerContextImpl::generateHashForSessionContextId(const std::vector<std::string>& server_names) {
  uint8_t hash_buffer[EVP_MAX_MD_SIZE];
  unsigned hash_length = 0;

  bssl::ScopedEVP_MD_CTX md;

  int rc = EVP_DigestInit(md.get(), EVP_sha256());
  RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));

  // Hash the CommonName/SANs of all the server certificates. This makes sure that sessions can only
  // be resumed to certificate(s) for the same name(s), but allows resuming to unique certs in the
  // case that different Envoy instances each have their own certs. All certificates in a
  // ServerContextImpl context are hashed together, since they all constitute a match on a filter
  // chain for resumption purposes.
  if (!capabilities_.provides_certificates) {
    for (const auto& ctx : tls_contexts_) {
      X509* cert = SSL_CTX_get0_certificate(ctx.ssl_ctx_.get());
      RELEASE_ASSERT(cert != nullptr, "TLS context should have an active certificate");
      X509_NAME* cert_subject = X509_get_subject_name(cert);
      RELEASE_ASSERT(cert_subject != nullptr, "TLS certificate should have a subject");

      const int cn_index = X509_NAME_get_index_by_NID(cert_subject, NID_commonName, -1);
      if (cn_index >= 0) {
        X509_NAME_ENTRY* cn_entry = X509_NAME_get_entry(cert_subject, cn_index);
        RELEASE_ASSERT(cn_entry != nullptr, "certificate subject CN should be present");

        ASN1_STRING* cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry);
        if (ASN1_STRING_length(cn_asn1) <= 0) {
          throwEnvoyExceptionOrPanic("Invalid TLS context has an empty subject CN");
        }

        rc = EVP_DigestUpdate(md.get(), ASN1_STRING_data(cn_asn1), ASN1_STRING_length(cn_asn1));
        RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
      }

      unsigned san_count = 0;
      bssl::UniquePtr<GENERAL_NAMES> san_names(static_cast<GENERAL_NAMES*>(
          X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)));

      if (san_names != nullptr) {
        for (const GENERAL_NAME* san : san_names.get()) {
          switch (san->type) {
          case GEN_IPADD:
            rc = EVP_DigestUpdate(md.get(), san->d.iPAddress->data, san->d.iPAddress->length);
            RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
            ++san_count;
            break;
          case GEN_DNS:
            rc = EVP_DigestUpdate(md.get(), ASN1_STRING_data(san->d.dNSName),
                                  ASN1_STRING_length(san->d.dNSName));
            RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
            ++san_count;
            break;
          case GEN_URI:
            rc = EVP_DigestUpdate(md.get(), ASN1_STRING_data(san->d.uniformResourceIdentifier),
                                  ASN1_STRING_length(san->d.uniformResourceIdentifier));
            RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
            ++san_count;
            break;
          }
        }
      }

      // It's possible that the certificate doesn't have a subject, but
      // does have SANs. Make sure that we have one or the other.
      if (cn_index < 0 && san_count == 0) {
        throwEnvoyExceptionOrPanic("Invalid TLS context has neither subject CN nor SAN names");
      }

      rc = X509_NAME_digest(X509_get_issuer_name(cert), EVP_sha256(), hash_buffer, &hash_length);
      RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
      RELEASE_ASSERT(hash_length == SHA256_DIGEST_LENGTH,
                     fmt::format("invalid SHA256 hash length {}", hash_length));

      rc = EVP_DigestUpdate(md.get(), hash_buffer, hash_length);
      RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
    }
  }

  cert_validator_->updateDigestForSessionId(md, hash_buffer, hash_length);

  // Hash configured SNIs for this context, so that sessions cannot be resumed across different
  // filter chains, even when using the same server certificate.
  for (const auto& name : server_names) {
    rc = EVP_DigestUpdate(md.get(), name.data(), name.size());
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
  }

  SessionContextID session_id;

  // Ensure that the output size of the hash we are using is no greater than
  // TLS session ID length that we want to generate.
  static_assert(session_id.size() == SHA256_DIGEST_LENGTH, "hash size mismatch");
  static_assert(session_id.size() == SSL_MAX_SSL_SESSION_ID_LENGTH, "TLS session ID size mismatch");

  rc = EVP_DigestFinal(md.get(), session_id.data(), &hash_length);
  RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
  RELEASE_ASSERT(hash_length == session_id.size(),
                 "SHA256 hash length must match TLS Session ID size");

  return session_id;
}

int ServerContextImpl::sessionTicketProcess(SSL*, uint8_t* key_name, uint8_t* iv,
                                            EVP_CIPHER_CTX* ctx, HMAC_CTX* hmac_ctx, int encrypt) {
  const EVP_MD* hmac = EVP_sha256();
  const EVP_CIPHER* cipher = EVP_aes_256_cbc();

  if (encrypt == 1) {
    // Encrypt
    RELEASE_ASSERT(!session_ticket_keys_.empty(), "");
    // TODO(ggreenway): validate in SDS that session_ticket_keys_ cannot be empty,
    // or if we allow it to be emptied, reconfigure the context so this callback
    // isn't set.

    const Envoy::Ssl::ServerContextConfig::SessionTicketKey& key = session_ticket_keys_.front();

    static_assert(std::tuple_size<decltype(key.name_)>::value == SSL_TICKET_KEY_NAME_LEN,
                  "Expected key.name length");
    std::copy_n(key.name_.begin(), SSL_TICKET_KEY_NAME_LEN, key_name);

    const int rc = RAND_bytes(iv, EVP_CIPHER_iv_length(cipher));
    ASSERT(rc);

    // This RELEASE_ASSERT is logically a static_assert, but we can't actually get
    // EVP_CIPHER_key_length(cipher) at compile-time
    RELEASE_ASSERT(key.aes_key_.size() == EVP_CIPHER_key_length(cipher), "");
    if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, key.aes_key_.data(), iv)) {
      return -1;
    }

    if (!HMAC_Init_ex(hmac_ctx, key.hmac_key_.data(), key.hmac_key_.size(), hmac, nullptr)) {
      return -1;
    }

    return 1; // success
  } else {
    // Decrypt
    bool is_enc_key = true; // first element is the encryption key
    for (const Envoy::Ssl::ServerContextConfig::SessionTicketKey& key : session_ticket_keys_) {
      static_assert(std::tuple_size<decltype(key.name_)>::value == SSL_TICKET_KEY_NAME_LEN,
                    "Expected key.name length");
      if (std::equal(key.name_.begin(), key.name_.end(), key_name)) {
        if (!HMAC_Init_ex(hmac_ctx, key.hmac_key_.data(), key.hmac_key_.size(), hmac, nullptr)) {
          return -1;
        }

        RELEASE_ASSERT(key.aes_key_.size() == EVP_CIPHER_key_length(cipher), "");
        if (!EVP_DecryptInit_ex(ctx, cipher, nullptr, key.aes_key_.data(), iv)) {
          return -1;
        }

        // If our current encryption was not the decryption key, renew
        return is_enc_key ? 1  // success; do not renew
                          : 2; // success: renew key
      }
      is_enc_key = false;
    }

    return 0; // decryption failed
  }
}

bool ServerContextImpl::isClientEcdsaCapable(const SSL_CLIENT_HELLO* ssl_client_hello) {
  CBS client_hello;
  CBS_init(&client_hello, ssl_client_hello->client_hello, ssl_client_hello->client_hello_len);

  // This is the TLSv1.3 case (TLSv1.2 on the wire and the supported_versions extensions present).
  // We just need to look at signature algorithms.
  const uint16_t client_version = ssl_client_hello->version;
  if (client_version == TLS1_2_VERSION && tls_max_version_ == TLS1_3_VERSION) {
    // If the supported_versions extension is found then we assume that the client is competent
    // enough that just checking the signature_algorithms is sufficient.
    const uint8_t* supported_versions_data;
    size_t supported_versions_len;
    if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_supported_versions,
                                             &supported_versions_data, &supported_versions_len)) {
      const uint8_t* signature_algorithms_data;
      size_t signature_algorithms_len;
      if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_signature_algorithms,
                                               &signature_algorithms_data,
                                               &signature_algorithms_len)) {
        CBS signature_algorithms_ext, signature_algorithms;
        CBS_init(&signature_algorithms_ext, signature_algorithms_data, signature_algorithms_len);
        if (!CBS_get_u16_length_prefixed(&signature_algorithms_ext, &signature_algorithms) ||
            CBS_len(&signature_algorithms_ext) != 0) {
          return false;
        }
        if (cbsContainsU16(signature_algorithms, SSL_SIGN_ECDSA_SECP256R1_SHA256)) {
          return true;
        }
      }

      return false;
    }
  }

  // Otherwise we are < TLSv1.3 and need to look at both the curves in the supported_groups for
  // ECDSA and also for a compatible cipher suite. https://tools.ietf.org/html/rfc4492#section-5.1.1
  const uint8_t* curvelist_data;
  size_t curvelist_len;
  if (!SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_supported_groups,
                                            &curvelist_data, &curvelist_len)) {
    return false;
  }

  CBS curvelist;
  CBS_init(&curvelist, curvelist_data, curvelist_len);

  // We only support P256 ECDSA curves today.
  if (!cbsContainsU16(curvelist, SSL_CURVE_SECP256R1)) {
    return false;
  }

  // The client must have offered an ECDSA ciphersuite that we like.
  CBS cipher_suites;
  CBS_init(&cipher_suites, ssl_client_hello->cipher_suites, ssl_client_hello->cipher_suites_len);

  while (CBS_len(&cipher_suites) > 0) {
    uint16_t cipher_id;
    if (!CBS_get_u16(&cipher_suites, &cipher_id)) {
      return false;
    }
    // All tls_context_ share the same set of enabled ciphers, so we can just look at the base
    // context.
    if (tls_contexts_[0].isCipherEnabled(cipher_id, client_version)) {
      return true;
    }
  }

  return false;
}

bool ServerContextImpl::isClientOcspCapable(const SSL_CLIENT_HELLO* ssl_client_hello) {
  const uint8_t* status_request_data;
  size_t status_request_len;
  if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_status_request,
                                           &status_request_data, &status_request_len)) {
    return true;
  }

  return false;
}

OcspStapleAction ServerContextImpl::ocspStapleAction(const TlsContext& ctx,
                                                     bool client_ocsp_capable) {
  if (!client_ocsp_capable) {
    return OcspStapleAction::ClientNotCapable;
  }

  auto& response = ctx.ocsp_response_;

  auto policy = ocsp_staple_policy_;
  if (ctx.is_must_staple_) {
    // The certificate has the must-staple extension, so upgrade the policy to match.
    policy = Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple;
  }

  const bool valid_response = response && !response->isExpired();

  switch (policy) {
  case Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling:
    if (!valid_response) {
      return OcspStapleAction::NoStaple;
    }
    return OcspStapleAction::Staple;

  case Ssl::ServerContextConfig::OcspStaplePolicy::StrictStapling:
    if (valid_response) {
      return OcspStapleAction::Staple;
    }
    if (response) {
      // Expired response.
      return OcspStapleAction::Fail;
    }
    return OcspStapleAction::NoStaple;

  case Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple:
    if (!valid_response) {
      return OcspStapleAction::Fail;
    }
    return OcspStapleAction::Staple;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

enum ssl_select_cert_result_t
ServerContextImpl::selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello) {
  const bool client_ecdsa_capable = isClientEcdsaCapable(ssl_client_hello);
  const bool client_ocsp_capable = isClientOcspCapable(ssl_client_hello);
  absl::string_view sni = absl::NullSafeStringView(
      SSL_get_servername(ssl_client_hello->ssl, TLSEXT_NAMETYPE_host_name));

  // selected_ctx represents the final selected certificate, it should meet all requirements or pick
  // a candidate
  const TlsContext* selected_ctx = nullptr;
  const TlsContext* candidate_ctx = nullptr;
  OcspStapleAction ocsp_staple_action;

  auto selected = [&](const TlsContext& ctx) -> bool {
    auto action = ocspStapleAction(ctx, client_ocsp_capable);
    if (action == OcspStapleAction::Fail) {
      // The selected ctx must adhere to OCSP policy
      return false;
    }

    if (client_ecdsa_capable == ctx.is_ecdsa_) {
      selected_ctx = &ctx;
      ocsp_staple_action = action;
      return true;
    }

    if (client_ecdsa_capable && !ctx.is_ecdsa_ && candidate_ctx == nullptr) {
      // ECDSA cert is preferred if client is ECDSA capable, so RSA cert is marked as a candidate,
      // searching will continue until exhausting all certs or find a exact match.
      candidate_ctx = &ctx;
      ocsp_staple_action = action;
      return false;
    }

    return false;
  };

  auto select_from_map = [this, &selected](absl::string_view server_name) -> void {
    auto it = server_names_map_.find(server_name);
    if (it == server_names_map_.end()) {
      return;
    }
    const auto& pkey_types_map = it->second;
    for (const auto& entry : pkey_types_map) {
      if (selected(entry.second.get())) {
        break;
      }
    }
  };

  auto tail_select = [&](bool go_to_next_phase) {
    if (selected_ctx == nullptr) {
      selected_ctx = candidate_ctx;
    }

    if (selected_ctx == nullptr && !go_to_next_phase) {
      selected_ctx = &tls_contexts_[0];
      ocsp_staple_action = ocspStapleAction(*selected_ctx, client_ocsp_capable);
    }
  };

  // Select cert based on SNI if SNI is provided by client.
  if (!sni.empty()) {
    // Match on exact server name, i.e. "www.example.com" for "www.example.com".
    select_from_map(sni);
    tail_select(true);

    if (selected_ctx == nullptr) {
      // Match on wildcard domain, i.e. ".example.com" for "www.example.com".
      // https://datatracker.ietf.org/doc/html/rfc6125#section-6.4
      size_t pos = sni.find('.', 1);
      if (pos < sni.size() - 1 && pos != std::string::npos) {
        absl::string_view wildcard = sni.substr(pos);
        select_from_map(wildcard);
      }
    }
    tail_select(full_scan_certs_on_sni_mismatch_);
  }
  // Full scan certs if SNI is not provided by client;
  // Full scan certs if client provides SNI but no cert matches to it,
  // it requires full_scan_certs_on_sni_mismatch is enabled.
  if (selected_ctx == nullptr) {
    candidate_ctx = nullptr;
    // Skip loop when there is no cert compatible to key type
    if (client_ecdsa_capable || (!client_ecdsa_capable && has_rsa_)) {
      for (const auto& ctx : tls_contexts_) {
        if (selected(ctx)) {
          break;
        }
      }
    }
    tail_select(false);
  }

  // Apply the selected context. This must be done before OCSP stapling below
  // since applying the context can remove the previously-set OCSP response.
  // This will only return NULL if memory allocation fails.
  RELEASE_ASSERT(SSL_set_SSL_CTX(ssl_client_hello->ssl, selected_ctx->ssl_ctx_.get()) != nullptr,
                 "");

  if (client_ocsp_capable) {
    stats_.ocsp_staple_requests_.inc();
  }

  switch (ocsp_staple_action) {
  case OcspStapleAction::Staple: {
    // We avoid setting the OCSP response if the client didn't request it, but doing so is safe.
    RELEASE_ASSERT(selected_ctx->ocsp_response_,
                   "OCSP response must be present under OcspStapleAction::Staple");
    auto& resp_bytes = selected_ctx->ocsp_response_->rawBytes();
    int rc = SSL_set_ocsp_response(ssl_client_hello->ssl, resp_bytes.data(), resp_bytes.size());
    RELEASE_ASSERT(rc != 0, "");
    stats_.ocsp_staple_responses_.inc();
  } break;
  case OcspStapleAction::NoStaple:
    stats_.ocsp_staple_omitted_.inc();
    break;
  case OcspStapleAction::Fail:
    stats_.ocsp_staple_failed_.inc();
    return ssl_select_cert_error;
  case OcspStapleAction::ClientNotCapable:
    break;
  }

  return ssl_select_cert_success;
}

bool TlsContext::isCipherEnabled(uint16_t cipher_id, uint16_t client_version) {
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

void TlsContext::loadCertificateChain(const std::string& data, const std::string& data_path) {
  cert_chain_file_path_ = data_path;
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(data.data()), data.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  cert_chain_.reset(PEM_read_bio_X509_AUX(bio.get(), nullptr, nullptr, nullptr));
  if (cert_chain_ == nullptr || !SSL_CTX_use_certificate(ssl_ctx_.get(), cert_chain_.get())) {
    logSslErrorChain();
    throwEnvoyExceptionOrPanic(
        absl::StrCat("Failed to load certificate chain from ", cert_chain_file_path_));
  }
  // Read rest of the certificate chain.
  while (true) {
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (cert == nullptr) {
      break;
    }
    if (!SSL_CTX_add_extra_chain_cert(ssl_ctx_.get(), cert.get())) {
      throwEnvoyExceptionOrPanic(
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
    throwEnvoyExceptionOrPanic(
        absl::StrCat("Failed to load certificate chain from ", cert_chain_file_path_));
  }
}

void TlsContext::loadPrivateKey(const std::string& data, const std::string& data_path,
                                const std::string& password) {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(data.data()), data.size()));
  RELEASE_ASSERT(bio != nullptr, "");
  bssl::UniquePtr<EVP_PKEY> pkey(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr,
                              !password.empty() ? const_cast<char*>(password.c_str()) : nullptr));

  if (pkey == nullptr || !SSL_CTX_use_PrivateKey(ssl_ctx_.get(), pkey.get())) {
    throwEnvoyExceptionOrPanic(fmt::format("Failed to load private key from {}, Cause: {}",
                                           data_path,
                                           Utility::getLastCryptoError().value_or("unknown")));
  }

  checkPrivateKey(pkey, data_path);
}

void TlsContext::loadPkcs12(const std::string& data, const std::string& data_path,
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
    throwEnvoyExceptionOrPanic(absl::StrCat("Failed to load pkcs12 from ", data_path));
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
    throwEnvoyExceptionOrPanic(absl::StrCat("Failed to load certificate from ", data_path));
  }
  if (temp_private_key == nullptr || !SSL_CTX_use_PrivateKey(ssl_ctx_.get(), pkey.get())) {
    throwEnvoyExceptionOrPanic(fmt::format("Failed to load private key from {}, Cause: {}",
                                           data_path,
                                           Utility::getLastCryptoError().value_or("unknown")));
  }

  checkPrivateKey(pkey, data_path);
}

void TlsContext::checkPrivateKey(const bssl::UniquePtr<EVP_PKEY>& pkey,
                                 const std::string& key_path) {
#ifdef BORINGSSL_FIPS
  // Verify that private keys are passing FIPS pairwise consistency tests.
  switch (EVP_PKEY_id(pkey.get())) {
  case EVP_PKEY_EC: {
    const EC_KEY* ecdsa_private_key = EVP_PKEY_get0_EC_KEY(pkey.get());
    if (!EC_KEY_check_fips(ecdsa_private_key)) {
      throwEnvoyExceptionOrPanic(fmt::format("Failed to load private key from {}, ECDSA key failed "
                                             "pairwise consistency test required in FIPS mode",
                                             key_path));
    }
  } break;
  case EVP_PKEY_RSA: {
    RSA* rsa_private_key = EVP_PKEY_get0_RSA(pkey.get());
    if (!RSA_check_fips(rsa_private_key)) {
      throwEnvoyExceptionOrPanic(fmt::format("Failed to load private key from {}, RSA key failed "
                                             "pairwise consistency test required in FIPS mode",
                                             key_path));
    }
  } break;
  }
#else
  UNREFERENCED_PARAMETER(pkey);
  UNREFERENCED_PARAMETER(key_path);
#endif
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#include "source/common/tls/server_context_impl.h"

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

namespace Extensions {
namespace TransportSockets {
namespace Tls {

Ssl::CurveNIDVector getClientCurveNIDSupported(CBS& cbs) {
  Ssl::CurveNIDVector cnsv{};
  while (CBS_len(&cbs) > 0) {
    uint16_t v;
    if (!CBS_get_u16(&cbs, &v)) {
      break;
    }
    // Check for values that refer to the `sigalgs` used in TLSv1.3 negotiation (left)
    // or their equivalent curve expressed in TLSv1.2 `TLSEXT_TYPE_supported_groups`
    // present in ClientHello (right).
    if (v == SSL_SIGN_ECDSA_SECP256R1_SHA256 || v == SSL_CURVE_SECP256R1) {
      cnsv.push_back(NID_X9_62_prime256v1);
    }
    if (v == SSL_SIGN_ECDSA_SECP384R1_SHA384 || v == SSL_CURVE_SECP384R1) {
      cnsv.push_back(NID_secp384r1);
    }
    if (v == SSL_SIGN_ECDSA_SECP521R1_SHA512 || v == SSL_CURVE_SECP521R1) {
      cnsv.push_back(NID_secp521r1);
    }
  }
  return cnsv;
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

absl::StatusOr<std::unique_ptr<ServerContextImpl>>
ServerContextImpl::create(Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
                          const std::vector<std::string>& server_names,
                          Server::Configuration::CommonFactoryContext& factory_context,
                          Ssl::ContextAdditionalInitFunc additional_init) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ServerContextImpl>(new ServerContextImpl(
      scope, config, server_names, factory_context, additional_init, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ServerContextImpl::ServerContextImpl(Stats::Scope& scope,
                                     const Envoy::Ssl::ServerContextConfig& config,
                                     const std::vector<std::string>& server_names,
                                     Server::Configuration::CommonFactoryContext& factory_context,
                                     Ssl::ContextAdditionalInitFunc additional_init,
                                     absl::Status& creation_status)
    : ContextImpl(scope, config, factory_context, additional_init, creation_status),
      session_ticket_keys_(config.sessionTicketKeys()),
      ocsp_staple_policy_(config.ocspStaplePolicy()) {
  if (!creation_status.ok()) {
    return;
  }
  // If creation failed, do not create the selector.
  tls_certificate_selector_ = config.tlsCertificateSelectorFactory()(config, *this);

  if (config.tlsCertificates().empty() && !config.capabilities().provides_certificates) {
    creation_status =
        absl::InvalidArgumentError("Server TlsCertificates must have a certificate specified");
    return;
  }

  // Compute the session context ID hash. We use all the certificate identities,
  // since we should have a common ID for session resumption no matter what cert
  // is used. We do this early because it can fail.
  absl::StatusOr<SessionContextID> id_or_error = generateHashForSessionContextId(server_names);
  SET_AND_RETURN_IF_NOT_OK(id_or_error.status(), creation_status);
  const SessionContextID& session_id = *id_or_error;

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
      SET_AND_RETURN_IF_NOT_OK(cert_validator_->addClientValidationContext(
                                   ctx.ssl_ctx_.get(), config.requireClientCertificate()),
                               creation_status);
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

    if (!config.preferClientCiphers()) {
      // Use server cipher preference based on config.
      SSL_CTX_set_options(ctx.ssl_ctx_.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);
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
        creation_status =
            absl::InvalidArgumentError("OCSP response is required for must-staple certificate");
        return;
      }
      if (ocsp_staple_policy_ == Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple) {
        creation_status =
            absl::InvalidArgumentError("Required OCSP response is missing from TLS context");
        return;
      }
    } else {
      auto response_or_error =
          Ocsp::OcspResponseWrapperImpl::create(ocsp_resp_bytes, factory_context_.timeSource());
      SET_AND_RETURN_IF_NOT_OK(response_or_error.status(), creation_status);
      if (!response_or_error.value()->matchesCertificate(*ctx.cert_chain_)) {
        creation_status =
            absl::InvalidArgumentError("OCSP response does not match its TLS certificate");
        return;
      }
      ctx.ocsp_response_ = std::move(response_or_error.value());
    }
  }
}

absl::StatusOr<ServerContextImpl::SessionContextID>
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
          return absl::InvalidArgumentError("Invalid TLS context has an empty subject CN");
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
        return absl::InvalidArgumentError(
            "Invalid TLS context has neither subject CN nor SAN names");
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

// Returns a list of client capabilities for ECDSA curves as NIDs. An empty vector indicates
// a client that is unable to handle ECDSA.
Ssl::CurveNIDVector
ServerContextImpl::getClientEcdsaCapabilities(const SSL_CLIENT_HELLO& ssl_client_hello) const {
  CBS client_hello;
  CBS_init(&client_hello, ssl_client_hello.client_hello, ssl_client_hello.client_hello_len);

  // This is the TLSv1.3 case (TLSv1.2 on the wire and the supported_versions extensions present).
  // We just need to look at signature algorithms.
  const uint16_t client_version = ssl_client_hello.version;
  if (client_version == TLS1_2_VERSION && tls_max_version_ == TLS1_3_VERSION) {
    // If the supported_versions extension is found then we assume that the client is competent
    // enough that just checking the signature_algorithms is sufficient.
    const uint8_t* supported_versions_data;
    size_t supported_versions_len;
    if (SSL_early_callback_ctx_extension_get(&ssl_client_hello, TLSEXT_TYPE_supported_versions,
                                             &supported_versions_data, &supported_versions_len)) {
      const uint8_t* signature_algorithms_data;
      size_t signature_algorithms_len;
      if (SSL_early_callback_ctx_extension_get(&ssl_client_hello, TLSEXT_TYPE_signature_algorithms,
                                               &signature_algorithms_data,
                                               &signature_algorithms_len)) {
        CBS signature_algorithms_ext, signature_algorithms;
        CBS_init(&signature_algorithms_ext, signature_algorithms_data, signature_algorithms_len);
        if (!CBS_get_u16_length_prefixed(&signature_algorithms_ext, &signature_algorithms) ||
            CBS_len(&signature_algorithms_ext) != 0) {
          return Ssl::CurveNIDVector{};
        }
        return getClientCurveNIDSupported(signature_algorithms);
      }

      return Ssl::CurveNIDVector{};
    }
  }

  // Otherwise we are < TLSv1.3 and need to look at both the curves in the supported_groups for
  // ECDSA and also for a compatible cipher suite. https://tools.ietf.org/html/rfc4492#section-5.1.1
  const uint8_t* curvelist_data;
  size_t curvelist_len;
  if (!SSL_early_callback_ctx_extension_get(&ssl_client_hello, TLSEXT_TYPE_supported_groups,
                                            &curvelist_data, &curvelist_len)) {
    return Ssl::CurveNIDVector{};
  }

  CBS curvelist;
  CBS_init(&curvelist, curvelist_data, curvelist_len);

  Ssl::CurveNIDVector client_capabilities = getClientCurveNIDSupported(curvelist);
  // if we haven't got any curves in common with the client, return empty CurveNIDVector.
  if (client_capabilities.empty()) {
    return Ssl::CurveNIDVector{};
  }

  // The client must have offered an ECDSA ciphersuite that we like.
  CBS cipher_suites;
  CBS_init(&cipher_suites, ssl_client_hello.cipher_suites, ssl_client_hello.cipher_suites_len);

  while (CBS_len(&cipher_suites) > 0) {
    uint16_t cipher_id;
    if (!CBS_get_u16(&cipher_suites, &cipher_id)) {
      return Ssl::CurveNIDVector{};
    }
    // All tls_context_ share the same set of enabled ciphers, so we can just look at the base
    // context.
    if (tls_contexts_[0].isCipherEnabled(cipher_id, client_version)) {
      return client_capabilities;
    }
  }

  return Ssl::CurveNIDVector{};
}

bool ServerContextImpl::isClientOcspCapable(const SSL_CLIENT_HELLO& ssl_client_hello) const {
  const uint8_t* status_request_data;
  size_t status_request_len;
  if (SSL_early_callback_ctx_extension_get(&ssl_client_hello, TLSEXT_TYPE_status_request,
                                           &status_request_data, &status_request_len)) {
    return true;
  }

  return false;
}

std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
ServerContextImpl::findTlsContext(absl::string_view sni,
                                  const Ssl::CurveNIDVector& client_ecdsa_capabilities,
                                  bool client_ocsp_capable, bool* cert_matched_sni) {
  return tls_certificate_selector_->findTlsContext(sni, client_ecdsa_capabilities,
                                                   client_ocsp_capable, cert_matched_sni);
}

enum ssl_select_cert_result_t
ServerContextImpl::selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello) {
  ASSERT(tls_certificate_selector_ != nullptr);

  auto* extended_socket_info = reinterpret_cast<Envoy::Ssl::SslExtendedSocketInfo*>(
      SSL_get_ex_data(ssl_client_hello->ssl, ContextImpl::sslExtendedSocketInfoIndex()));

  auto selection_result = extended_socket_info->certificateSelectionResult();
  switch (selection_result) {
  case Ssl::CertificateSelectionStatus::NotStarted:
    // continue
    break;

  case Ssl::CertificateSelectionStatus::Pending:
    ENVOY_LOG(trace, "already waiting certificate");
    return ssl_select_cert_retry;

  case Ssl::CertificateSelectionStatus::Successful:
    ENVOY_LOG(trace, "wait certificate success");
    return ssl_select_cert_success;

  default:
    ENVOY_LOG(trace, "wait certificate failed");
    return ssl_select_cert_error;
  }

  ENVOY_LOG(trace, "TLS context selection result: {}, before selectTlsContext",
            static_cast<int>(selection_result));

  const auto result = tls_certificate_selector_->selectTlsContext(
      *ssl_client_hello, extended_socket_info->createCertificateSelectionCallback());

  ENVOY_LOG(trace,
            "TLS context selection result: {}, after selectTlsContext, selection result status: {}",
            static_cast<int>(extended_socket_info->certificateSelectionResult()),
            static_cast<int>(result.status));
  ASSERT(extended_socket_info->certificateSelectionResult() ==
             Ssl::CertificateSelectionStatus::Pending,
         "invalid selection result");

  switch (result.status) {
  case Ssl::SelectionResult::SelectionStatus::Success:
    extended_socket_info->onCertificateSelectionCompleted(*result.selected_ctx, result.staple,
                                                          false);
    return ssl_select_cert_success;
  case Ssl::SelectionResult::SelectionStatus::Pending:
    return ssl_select_cert_retry;
  case Ssl::SelectionResult::SelectionStatus::Failed:
    extended_socket_info->onCertificateSelectionCompleted(OptRef<const Ssl::TlsContext>(), false,
                                                          false);
    return ssl_select_cert_error;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

absl::StatusOr<Ssl::ServerContextSharedPtr> ServerContextFactoryImpl::createServerContext(
    Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
    const std::vector<std::string>& server_names,
    Server::Configuration::CommonFactoryContext& factory_context,
    Ssl::ContextAdditionalInitFunc additional_init) {
  return ServerContextImpl::create(scope, config, server_names, factory_context,
                                   std::move(additional_init));
}

REGISTER_FACTORY(ServerContextFactoryImpl, ServerContextFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

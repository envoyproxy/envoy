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

} // namespace

namespace Extensions {
namespace TransportSockets {
namespace Tls {

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
      ocsp_staple_policy_(config.ocspStaplePolicy()),
      full_scan_certs_on_sni_mismatch_(config.fullScanCertsOnSNIMismatch()) {
  if (!creation_status.ok()) {
    return;
  }
  if (config.tlsCertificates().empty() && !config.capabilities().provides_certificates) {
    creation_status =
        absl::InvalidArgumentError("Server TlsCertificates must have a certificate specified");
    return;
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

void ServerContextImpl::populateServerNamesMap(Ssl::TlsContext& ctx, int pkey_id) {
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
    sn_match->second.emplace(std::pair<int, std::reference_wrapper<Ssl::TlsContext>>(pkey_id, ctx));
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

OcspStapleAction ServerContextImpl::ocspStapleAction(const Ssl::TlsContext& ctx,
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

std::pair<const Ssl::TlsContext&, OcspStapleAction>
ServerContextImpl::findTlsContext(absl::string_view sni, bool client_ecdsa_capable,
                                  bool client_ocsp_capable, bool* cert_matched_sni) {
  bool unused = false;
  if (cert_matched_sni == nullptr) {
    // Avoid need for nullptr checks when this is set.
    cert_matched_sni = &unused;
  }

  // selected_ctx represents the final selected certificate, it should meet all requirements or pick
  // a candidate.
  const Ssl::TlsContext* selected_ctx = nullptr;
  const Ssl::TlsContext* candidate_ctx = nullptr;
  OcspStapleAction ocsp_staple_action;

  auto selected = [&](const Ssl::TlsContext& ctx) -> bool {
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
    *cert_matched_sni = (selected_ctx != nullptr || candidate_ctx != nullptr);
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

  ASSERT(selected_ctx != nullptr);
  return {*selected_ctx, ocsp_staple_action};
}

enum ssl_select_cert_result_t
ServerContextImpl::selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello) {
  absl::string_view sni = absl::NullSafeStringView(
      SSL_get_servername(ssl_client_hello->ssl, TLSEXT_NAMETYPE_host_name));
  const bool client_ecdsa_capable = isClientEcdsaCapable(ssl_client_hello);
  const bool client_ocsp_capable = isClientOcspCapable(ssl_client_hello);

  auto [selected_ctx, ocsp_staple_action] =
      findTlsContext(sni, client_ecdsa_capable, client_ocsp_capable, nullptr);

  // Apply the selected context. This must be done before OCSP stapling below
  // since applying the context can remove the previously-set OCSP response.
  // This will only return NULL if memory allocation fails.
  RELEASE_ASSERT(SSL_set_SSL_CTX(ssl_client_hello->ssl, selected_ctx.ssl_ctx_.get()) != nullptr,
                 "");

  if (client_ocsp_capable) {
    stats_.ocsp_staple_requests_.inc();
  }

  switch (ocsp_staple_action) {
  case OcspStapleAction::Staple: {
    // We avoid setting the OCSP response if the client didn't request it, but doing so is safe.
    RELEASE_ASSERT(selected_ctx.ocsp_response_,
                   "OCSP response must be present under OcspStapleAction::Staple");
    auto& resp_bytes = selected_ctx.ocsp_response_->rawBytes();
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

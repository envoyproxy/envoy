#include "extensions/transport_sockets/tls/cert_validator/spiffe_validator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "common/common/matchers.h"
#include "common/common/regex.h"
#include "common/config/datasource.h"
#include "common/config/utility.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/transport_sockets/tls/cert_validator/factory.h"
#include "extensions/transport_sockets/tls/stats.h"
#include "extensions/transport_sockets/tls/utility.h"

#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using SPIFFEConfig = envoy::extensions::transport_sockets::tls::v3::SPIFFECertValidatorConfig;

SPIFFEValidator::SPIFFEValidator(Envoy::Ssl::CertificateValidationContextConfig* config,
                                 TimeSource& time_source)
    : time_source_(time_source) {
  if (config == nullptr) {
    throw EnvoyException("SPIFFE cert validator connot be initialized from null configuration");
  }

  SPIFFEConfig message;
  Config::Utility::translateOpaqueConfig(config->customValidatorConfig().value().typed_config(),
                                         ProtobufWkt::Struct(),
                                         ProtobufMessage::getStrictValidationVisitor(), message);

  auto size = message.trust_bundles().size();
  if (size == 0) {
    throw EnvoyException("SPIFFE cert validator requires at least one trusted CA");
  }

  trust_bundle_stores_.reserve(size);
  for (auto& it : message.trust_bundles()) {
    auto cert = Config::DataSource::read(it.second, true, config->api());
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(cert.data()), cert.size()));
    RELEASE_ASSERT(bio != nullptr, "");
    bssl::UniquePtr<STACK_OF(X509_INFO)> list(
        PEM_X509_INFO_read_bio(bio.get(), nullptr, nullptr, nullptr));
    if (list == nullptr) {
      throw EnvoyException(absl::StrCat("Failed to load trusted CA certificate for ", it.first));
    }

    auto store = X509StorePtr(X509_STORE_new());
    bool has_crl = false;
    bool ca_loaded = false;
    for (const X509_INFO* item : list.get()) {
      if (item->x509) {
        X509_STORE_add_cert(store.get(), item->x509);
        X509_up_ref(item->x509);
        ca_certs_.push_back(bssl::UniquePtr<X509>(item->x509));
        if (!ca_loaded) {
          // TODO: with the current interface, we cannot return the multiple
          // cert information on getCaCertInformation method.
          // So temporarily we return the first CA's info here.
          ca_loaded = true;
          auto name = it.second.filename();
          if (name.empty()) {
            name = "<inline>";
          }
          ca_file_name_ = absl::StrCat(it.first, ": ", name);
        }
      }

      if (item->crl) {
        has_crl = true;
        X509_STORE_add_crl(store.get(), item->crl);
      }
    }
    if (has_crl) {
      X509_STORE_set_flags(store.get(), X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
    }
    trust_bundle_stores_[it.first] = std::move(store);
  }
}

void SPIFFEValidator::addClientValidationContext(SSL_CTX* ctx, bool) {
  bssl::UniquePtr<STACK_OF(X509_NAME)> list(sk_X509_NAME_new(
      [](const X509_NAME** a, const X509_NAME** b) -> int { return X509_NAME_cmp(*a, *b); }));

  for (auto& ca : ca_certs_) {
    X509_NAME* name = X509_get_subject_name(ca.get());
    if (name == nullptr) {
      throw EnvoyException(absl::StrCat("Failed to load trusted client CA certificate"));
    }
    // Check for duplicates.
    if (sk_X509_NAME_find(list.get(), nullptr, name)) {
      continue;
    }
    bssl::UniquePtr<X509_NAME> name_dup(X509_NAME_dup(name));
    if (name_dup == nullptr || !sk_X509_NAME_push(list.get(), name_dup.release())) {
      throw EnvoyException(absl::StrCat("Failed to load trusted client CA certificate"));
    }
  }
  SSL_CTX_set_client_CA_list(ctx, list.release());
}

void SPIFFEValidator::updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md,
                                               uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                               unsigned hash_length) {
  int rc;
  for (auto& ca : ca_certs_) {
    rc = X509_digest(ca.get(), EVP_sha256(), hash_buffer, &hash_length);
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
    RELEASE_ASSERT(hash_length == SHA256_DIGEST_LENGTH,
                   fmt::format("invalid SHA256 hash length {}", hash_length));
    rc = EVP_DigestUpdate(md.get(), hash_buffer, hash_length);
    RELEASE_ASSERT(rc == 1, Utility::getLastCryptoError().value_or(""));
  }
}

int SPIFFEValidator::initializeSslContexts(std::vector<SSL_CTX*>, bool) {
  return SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
}

int SPIFFEValidator::doVerifyCertChain(X509_STORE_CTX* store_ctx,
                                       Ssl::SslExtendedSocketInfo* ssl_extended_info,
                                       X509& leaf_cert, const Network::TransportSocketOptions*) {
  if (!SPIFFEValidator::certificatePrecheck(&leaf_cert)) {
    if (ssl_extended_info) {
      ssl_extended_info->setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus::Failed);
    }
    return 0;
  }

  auto trust_bundle = getTrustBundleStore(&leaf_cert);
  if (!trust_bundle) {
    if (ssl_extended_info) {
      ssl_extended_info->setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus::Failed);
    }
    return 0;
  }

  // set the trust bundle's certificate store on the context, and do the verification
  store_ctx->ctx = trust_bundle;
  auto ret = X509_verify_cert(store_ctx);
  if (ssl_extended_info) {
    ssl_extended_info->setCertificateValidationStatus(
        ret == 1 ? Envoy::Ssl::ClientValidationStatus::Validated
                 : Envoy::Ssl::ClientValidationStatus::Failed);
  }
  return ret;
}

X509_STORE* SPIFFEValidator::getTrustBundleStore(X509* leaf_cert) {
  bssl::UniquePtr<GENERAL_NAMES> san_names(static_cast<GENERAL_NAMES*>(
      X509_get_ext_d2i(leaf_cert, NID_subject_alt_name, nullptr, nullptr)));
  if (san_names == nullptr) {
    return nullptr;
  }

  std::string trust_domain;
  for (const GENERAL_NAME* general_name : san_names.get()) {
    const std::string san = Utility::generalNameAsString(general_name);
    trust_domain = SPIFFEValidator::extractTrustDomain(san);
    // we can assume that valid SVID has only one san
    break;
  }

  if (trust_domain.empty()) {
    return nullptr;
  }

  auto target_store = trust_bundle_stores_.find(trust_domain);
  if (target_store == trust_bundle_stores_.end()) {
    return nullptr;
  }

  return target_store->second.get();
}

bool SPIFFEValidator::certificatePrecheck(X509* leaf_cert) {
  // Check basic constrains and key usage
  // https://github.com/spiffe/spiffe/blob/master/standards/X509-SVID.md#52-leaf-validation
  auto ext = X509_get_extension_flags(leaf_cert);
  if (ext & EXFLAG_CA) {
    return false;
  }

  auto us = X509_get_key_usage(leaf_cert);
  return (us & (KU_CRL_SIGN | KU_KEY_CERT_SIGN)) == 0;
}

std::string SPIFFEValidator::extractTrustDomain(const std::string& san) {
  static const std::regex reg = Envoy::Regex::Utility::parseStdRegex("spiffe:\\/\\/([^\\/]+)\\/");
  std::smatch m;

  if (!std::regex_search(san, m, reg) || m.size() < 2) {
    return "";
  }
  return m[1];
}

size_t SPIFFEValidator::daysUntilFirstCertExpires() const {
  if (ca_certs_.empty()) {
    return 0;
  }
  // TODO: with the current interface, we cannot pass the multiple cert information.
  // So temporarily we return the first CA's info here.
  return Utility::getDaysUntilExpiration(ca_certs_[0].get(), time_source_);
}

Envoy::Ssl::CertificateDetailsPtr SPIFFEValidator::getCaCertInformation() const {
  if (ca_certs_.empty()) {
    return nullptr;
  }
  // TODO: with the current interface, we cannot pass the multiple cert information.
  // So temporarily we return the first CA's info here.
  return Utility::certificateDetails(ca_certs_[0].get(), getCaFileName(), time_source_);
};

class SPIFFEValidatorFactory : public CertValidatorFactory {
public:
  CertValidatorPtr createCertValidator(Envoy::Ssl::CertificateValidationContextConfig* config,
                                       SslStats&, TimeSource& time_source) override {
    return std::make_unique<SPIFFEValidator>(config, time_source);
  }

  absl::string_view name() override { return "envoy.tls.cert_validator.spiffe"; }
};

REGISTER_FACTORY(SPIFFEValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

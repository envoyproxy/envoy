#include "extensions/transport_sockets/tls/cert_validator/spiffe_validator.h"

#include <array>
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

SPIFFEValidator::SPIFFEValidator(Envoy::Ssl::CertificateValidationContextConfig* config) {
  if (config == nullptr) {
    throw EnvoyException("SPIFFE validator connot be initialized from null configuration");
  }

  SPIFFEConfig message;
  Config::Utility::translateOpaqueConfig(config->customValidatorConfig().value().typed_config(),
                                         ProtobufWkt::Struct(),
                                         ProtobufMessage::getStrictValidationVisitor(), message);

  trust_bundle_stores_.reserve(message.trust_bundles().size());
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
    for (const X509_INFO* item : list.get()) {
      if (item->x509) {
        X509_STORE_add_cert(store.get(), item->x509);
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

void SPIFFEValidator::addClientValidationContext(SSL_CTX*, bool) { /* TODO */
}

void SPIFFEValidator::updateDigestForSessionId(bssl::ScopedEVP_MD_CTX&, uint8_t[EVP_MAX_MD_SIZE],
                                               unsigned) { /* TODO */
}

int SPIFFEValidator::initializeSslContexts(std::vector<SSL_CTX*>, bool) { return SSL_VERIFY_PEER; }

int SPIFFEValidator::doVerifyCertChain(X509_STORE_CTX* store_ctx, Ssl::SslExtendedSocketInfo*,
                                       X509& leaf_cert, const Network::TransportSocketOptions*) {
  if (!SPIFFEValidator::certificatePrecheck(&leaf_cert)) {
    return 0;
  }

  auto trust_bundle = getTrustBundleStore(&leaf_cert);
  if (!trust_bundle) {
    return 0;
  }
  store_ctx->ctx = trust_bundle;
  return X509_verify_cert(store_ctx);
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
    // we can assume that valid SVIDs have only one san
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

int SPIFFEValidator::certificatePrecheck(X509* leaf_cert) {
  // Check basic constrains and key usage
  // https://github.com/spiffe/spiffe/blob/master/standards/X509-SVID.md#52-leaf-validation
  auto ext = X509_get_extension_flags(leaf_cert);
  if (ext & EXFLAG_CA) {
    return 0;
  }

  auto us = X509_get_key_usage(leaf_cert);
  return !(us & KU_CRL_SIGN) && !(us & KU_KEY_CERT_SIGN);
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
  /* TODO */
  return 0;
}

std::string SPIFFEValidator::getCaFileName() const {
  /* TODO */
  return "";
}

Envoy::Ssl::CertificateDetailsPtr SPIFFEValidator::getCaCertInformation() const {
  /* TODO */
  return nullptr;
};

class SPIFFEValidatorFactory : public CertValidatorFactory {
public:
  CertValidatorPtr createCertValidator(Envoy::Ssl::CertificateValidationContextConfig* config,
                                       SslStats&, TimeSource&) override {
    return std::make_unique<SPIFFEValidator>(config);
  }

  absl::string_view name() override { return "envoy.tls.cert_validator.spiffe"; }
};

REGISTER_FACTORY(SPIFFEValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

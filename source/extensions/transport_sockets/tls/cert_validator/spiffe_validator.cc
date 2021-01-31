#include "extensions/transport_sockets/tls/cert_validator/spiffe_validator.h"

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/registry/registry.h"
#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.pb.h"

#include "common/common/matchers.h"
#include "common/config/utility.h"
#include "common/stats/symbol_table_impl.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/config/datasource.h"

#include "extensions/transport_sockets/tls/stats.h"
#include "extensions/transport_sockets/tls/cert_validator/factory.h"

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
    // TODO(@mathetake): check "spiffe://" prefix?
    auto cert = Config::DataSource::read(it.second, true, config->api());
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(cert.data()), cert.size()));
    RELEASE_ASSERT(bio != nullptr, "");
    bssl::UniquePtr<STACK_OF(X509_INFO)> list(
        PEM_X509_INFO_read_bio(bio.get(), nullptr, nullptr, nullptr));
    if (list == nullptr) {
      throw EnvoyException(absl::StrCat("Failed to load trusted CA certificate for ", it.first));
    }

    auto store = X509StorePtr(X509_STORE_new());
    for (const X509_INFO* item : list.get()) {
      if (item->x509) {
        X509_STORE_add_cert(store.get(), item->x509);
      }
      if (item->crl) {
        X509_STORE_add_crl(store.get(), item->crl);
      }
    }
    store.get();
    trust_bundle_stores_[it.first] = std::move(store);
  }
}

void SPIFFEValidator::addClientValidationContext(SSL_CTX*, bool require_client_cert) {}

void SPIFFEValidator::updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md,
                                               uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                               unsigned hash_length) {}

int SPIFFEValidator::initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                           bool provides_certificates) {
  return 0;
}

int SPIFFEValidator::doVerifyCertChain(
    X509_STORE_CTX* store_ctx, Ssl::SslExtendedSocketInfo* ssl_extended_info, X509& leaf_cert,
    const Network::TransportSocketOptions* transport_socket_options) {
  return 0;
}

class SPIFFEValidatorFactory : public CertValidatorFactory {
public:
  CertValidatorPtr createCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                                       SslStats& stats, TimeSource& time_source) override {
    return std::make_unique<SPIFFEValidator>(config);
  }

  absl::string_view name() override { return "envoy.tls.cert_validator.spiffe"; }
};

REGISTER_FACTORY(SPIFFEValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

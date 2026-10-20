#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModules {

using DynamicModuleTlsCertificateSelectorProto = envoy::extensions::transport_sockets::tls::
    cert_selectors::dynamic_modules::v3::DynamicModuleTlsCertificateSelector;

/**
 * Config factory for dynamic module certificate selectors of downstream TLS contexts.
 */
class DynamicModuleTlsCertificateSelectorConfigFactory
    : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  std::string name() const override { return "envoy.tls.certificate_selectors.dynamic_modules"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<DynamicModuleTlsCertificateSelectorProto>();
  }

  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig& tls_config,
                                      bool for_quic) override;
};

/**
 * Config factory for dynamic module certificate selectors of upstream TLS contexts.
 */
class DynamicModuleUpstreamTlsCertificateSelectorConfigFactory
    : public Ssl::UpstreamTlsCertificateSelectorConfigFactory {
public:
  std::string name() const override { return "envoy.tls.certificate_selectors.dynamic_modules"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<DynamicModuleTlsCertificateSelectorProto>();
  }

  absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
  createUpstreamTlsCertificateSelectorFactory(
      const Protobuf::Message& config,
      Server::Configuration::GenericFactoryContext& factory_context,
      const Ssl::ClientContextConfig& tls_config) override;
};

DECLARE_FACTORY(DynamicModuleTlsCertificateSelectorConfigFactory);
DECLARE_FACTORY(DynamicModuleUpstreamTlsCertificateSelectorConfigFactory);

} // namespace DynamicModules
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

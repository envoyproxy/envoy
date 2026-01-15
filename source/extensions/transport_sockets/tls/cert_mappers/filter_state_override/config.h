#pragma once

#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace FilterStateOverride {

using ConfigProto =
    envoy::extensions::transport_sockets::tls::cert_mappers::filter_state_override::v3::Config;

class MapperFactory : public Ssl::UpstreamTlsCertificateMapperConfigFactory {
public:
  absl::StatusOr<Ssl::UpstreamTlsCertificateMapperFactory> createTlsCertificateMapperFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override {
    return "envoy.tls.upstream_certificate_mappers.filter_state_override";
  }
};

DECLARE_FACTORY(MapperFactory);

} // namespace FilterStateOverride
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace SNI {

using SNIConfigProto = envoy::extensions::transport_sockets::tls::cert_mappers::sni::v3::SNI;
class SNIMapperFactory : public Ssl::TlsCertificateMapperConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateMapperFactory> createTlsCertificateMapperFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<SNIConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_mappers.sni"; }
};

DECLARE_FACTORY(SNIMapperFactory);

} // namespace SNI
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

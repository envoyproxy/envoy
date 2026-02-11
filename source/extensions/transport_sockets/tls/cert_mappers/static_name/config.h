#pragma once

#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace StaticName {

using StaticNameConfigProto =
    envoy::extensions::transport_sockets::tls::cert_mappers::static_name::v3::StaticName;

constexpr absl::string_view StaticNameExtension = "envoy.tls.certificate_mappers.static_name";

class StaticNameMapperFactory : public Ssl::TlsCertificateMapperConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateMapperFactory> createTlsCertificateMapperFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<StaticNameConfigProto>();
  }

  std::string name() const override { return std::string(StaticNameExtension); }
};

DECLARE_FACTORY(StaticNameMapperFactory);

class UpstreamStaticNameMapperFactory : public Ssl::UpstreamTlsCertificateMapperConfigFactory {
public:
  absl::StatusOr<Ssl::UpstreamTlsCertificateMapperFactory> createTlsCertificateMapperFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<StaticNameConfigProto>();
  }

  std::string name() const override { return std::string(StaticNameExtension); }
};

DECLARE_FACTORY(UpstreamStaticNameMapperFactory);

} // namespace StaticName
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

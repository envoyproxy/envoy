#pragma once

#include "envoy/extensions/certificate_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/certificate_mappers/static_name/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"

namespace Envoy {
namespace Extensions {
namespace CertificateMappers {
namespace StaticName {

using StaticNameConfigProto = envoy::extensions::certificate_mappers::static_name::v3::StaticName;
class StaticNameMapperFactory : public Ssl::TlsCertificateMapperConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateMapperFactory> createTlsCertificateMapperFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<StaticNameConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_mappers.static_name"; }
};

DECLARE_FACTORY(StaticNameMapperFactory);

} // namespace StaticName
} // namespace CertificateMappers
} // namespace Extensions
} // namespace Envoy

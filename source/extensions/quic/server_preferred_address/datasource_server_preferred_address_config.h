#pragma once

#include "envoy/extensions/quic/server_preferred_address/v3/datasource.pb.h"
#include "envoy/extensions/quic/server_preferred_address/v3/datasource.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_server_preferred_address_config_factory.h"

namespace Envoy {
namespace Quic {

// This method of configuring server preferred address allows fetching the config from
// a `DataSource`, for situations where the control plane doesn't know the correct value
// but it is available in the context in which Envoy is running.
class DataSourceServerPreferredAddressConfigFactory
    : public Envoy::Quic::EnvoyQuicServerPreferredAddressConfigFactory {
public:
  std::string name() const override { return "quic.server_preferred_address.datasource"; }

  Envoy::Quic::EnvoyQuicServerPreferredAddressConfigPtr
  createServerPreferredAddressConfig(const Protobuf::Message& message,
                                     ProtobufMessage::ValidationVisitor& validation_visitor,
                                     Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::quic::server_preferred_address::v3::
                                         DataSourceServerPreferredAddressConfig()};
  }
};

DECLARE_FACTORY(DataSourceServerPreferredAddressConfigFactory);

} // namespace Quic
} // namespace Envoy

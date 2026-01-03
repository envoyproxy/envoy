#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

class DynamicModuleUdpListenerFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter>();
  }

  std::string name() const override { return "envoy.filters.udp_listener.dynamic_modules"; }
};

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

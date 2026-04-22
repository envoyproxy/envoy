#pragma once

#include "envoy/extensions/filters/listener/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/listener/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ListenerFilterConfig =
    envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter;

class DynamicModuleListenerFilterConfigFactory : public NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& config,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ListenerFilterConfig>();
  }

  std::string name() const override { return "envoy.filters.listener.dynamic_modules"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy

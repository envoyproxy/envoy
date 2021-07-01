#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/original_dst/config.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

/**
 * Config registration for the original dst filter. @see NamedNetworkFilterConfigFactory.
 */
class OriginalDstConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::listener::original_dst::v3::OriginalDst>();
  }

  std::string name() const override { return "envoy.filters.listener.original_dst"; }
};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

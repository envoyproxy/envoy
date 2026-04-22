#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {

/**
 * Config registration for the Postgres inspector filter. @see NamedListenerFilterConfigFactory.
 */
class PostgresInspectorConfigFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

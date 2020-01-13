#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {
/**
 * Config registration for the original_src filter.
 */
class OriginalSrcConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& message,
                               Server::Configuration::ListenerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return ListenerFilterNames::get().OriginalSrc; }
};

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy

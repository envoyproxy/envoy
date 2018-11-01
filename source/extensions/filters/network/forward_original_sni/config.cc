#include "extensions/filters/network/forward_original_sni/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/forward_original_sni/forward_original_sni.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSni {

Network::FilterFactoryCb
OriginalSniNetworkFilterConfigFactory::createFilterFactory(const Json::Object&,
                                                           Server::Configuration::FactoryContext&) {
  // Only used in v1 filters.
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Network::FilterFactoryCb OriginalSniNetworkFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message&, Server::Configuration::FactoryContext&) {
  return [](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<OriginalSniFilter>());
  };
}

ProtobufTypes::MessagePtr OriginalSniNetworkFilterConfigFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::Empty>();
}

/**
 * Static registration for the sni_cluster filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<OriginalSniNetworkFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace OriginalSni
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

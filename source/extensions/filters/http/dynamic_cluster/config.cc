#include "extensions/filters/http/dynamic_cluster/config.h"

#include <chrono>
#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/dynamic_cluster/dynamic_cluster.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicCluster {

Http::FilterFactoryCb
DynamicClusterFilterConfig::createFilter(const std::string&,
                                         Server::Configuration::FactoryContext& context) {
  return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new DynamicCluster(context.clusterManager())});
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<DynamicClusterFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace DynamicCluster
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

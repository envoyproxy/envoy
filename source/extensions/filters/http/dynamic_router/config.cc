#include "extensions/filters/http/dynamic_router/config.h"

#include <chrono>
#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/dynamic_router/dynamic_router.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicRouter {

Http::FilterFactoryCb
DynamicRouterFilterConfig::createFilter(const std::string&,
                                  Server::Configuration::FactoryContext& context) {
  return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
          Http::StreamDecoderFilterSharedPtr{new DynamicRouter(context.clusterManager(),context.dispatcher())});
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<DynamicRouterFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

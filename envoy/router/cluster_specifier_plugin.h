#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Router {

/**
 * Interface class for creating cluster specifier plugin from related route entry.
 */
class ClusterSpecifierPlugin {
public:
  virtual ~ClusterSpecifierPlugin() = default;

  /**
   * Create route from related route entry and request headers.
   *
   * @param parent related route.
   * @param headers request headers.
   * @param stream_info stream info of the downstream request.
   * @param random random value for cluster selection.
   * @return RouteConstSharedPtr final route with specific cluster.
   */
  virtual RouteConstSharedPtr route(RouteEntryAndRouteConstSharedPtr parent,
                                    const Http::RequestHeaderMap& headers,
                                    const StreamInfo::StreamInfo& stream_info,
                                    uint64_t random) const PURE;
};

using ClusterSpecifierPluginSharedPtr = std::shared_ptr<ClusterSpecifierPlugin>;

/*
 * Extension configuration for cluster specifier plugin factory.
 */
class ClusterSpecifierPluginFactoryConfig : public Envoy::Config::TypedFactory {
public:
  /**
   * Creates a particular cluster specifier plugin factory implementation.
   *
   * @param config supplies the configuration for the cluster specifier plugin factory extension.
   * @return ClusterSpecifierPluginSharedPtr cluster specifier plugin use to create final route from
   * request headers.
   */
  virtual ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.router.cluster_specifier_plugin"; }
};

using ClusterSpecifierPluginFactoryConfigPtr = std::unique_ptr<ClusterSpecifierPluginFactoryConfig>;

} // namespace Router
} // namespace Envoy

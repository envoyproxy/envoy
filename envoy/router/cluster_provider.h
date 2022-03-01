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
 * Interface class for creating cluster provider from related route entry.
 */
class ClusterProvider {
public:
  virtual ~ClusterProvider() = default;

  /**
   * Create route from related route entry and request headers.
   *
   * @param parent related route entry.
   * @param header request headers.
   * @return ClusterProvider cluster provider use to create final route from request headers.
   */
  virtual RouteConstSharedPtr route(const RouteEntry& parent,
                                    const Http::RequestHeaderMap& header) const PURE;
};

using ClusterProviderSharedPtr = std::shared_ptr<ClusterProvider>;

/*
 * Extension configuration for cluster provider factory.
 */
class ClusterProviderFactoryConfig : public Envoy::Config::TypedFactory {
public:
  /**
   * Creates a particular cluster provider factory implementation.
   *
   * @param config supplies the configuration for the cluster provider factory extension.
   * @return ClusterProviderFactorySharedPtr the cluster provider factory.
   */
  virtual ClusterProviderSharedPtr
  createClusterProvider(const Protobuf::Message& config,
                        Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override { return "envoy.route.cluster_provider"; }
};

using ClusterProviderFactoryConfigPtr = std::unique_ptr<ClusterProviderFactoryConfig>;

} // namespace Router
} // namespace Envoy

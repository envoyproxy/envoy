#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Router {

/**
 * Interface for a route override, a superset of the cluster specifier plugin. It runs at resolution
 * time with the configured route as the base and may override the cluster, timeouts, retries,
 * redirect, direct response, and other route fields based on the request.
 */
class RouteOverride {
public:
  virtual ~RouteOverride() = default;

  /**
   * Validate that the clusters the override may select exist in the cluster manager. The derived
   * class should override it when validation is needed.
   *
   * @param cluster_manager the cluster manager.
   * @return absl::Status the validation status.
   */
  virtual absl::Status validateClusters(const Upstream::ClusterManager&) const {
    return absl::OkStatus();
  }

  /**
   * Build the route for a request from the base route.
   *
   * @param parent the base route the override starts from.
   * @param headers the request headers.
   * @param stream_info the stream info of the downstream request.
   * @param random the random value for any runtime choice.
   * @return RouteConstSharedPtr the route with overrides applied.
   */
  virtual RouteConstSharedPtr route(RouteEntryAndRouteConstSharedPtr parent,
                                    const Http::RequestHeaderMap& headers,
                                    const StreamInfo::StreamInfo& stream_info,
                                    uint64_t random) const PURE;
};

using RouteOverrideSharedPtr = std::shared_ptr<RouteOverride>;

/**
 * Extension configuration for a route override factory.
 */
class RouteOverrideFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Create a route override from the extension config.
   *
   * @param config the configuration for the route override extension.
   * @param context the server factory context.
   * @return RouteOverrideSharedPtr the route override used to build the final route from the
   * request.
   */
  virtual absl::StatusOr<RouteOverrideSharedPtr>
  createRouteOverride(const Protobuf::Message& config,
                      Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.router.route_override"; }
};

} // namespace Router
} // namespace Envoy

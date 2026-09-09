#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Router {

/**
 * A route extension customizes the route used for a request. Extensions are configured as an
 * ordered list at the route configuration, virtual host and route levels, and run as a chain where
 * each extension receives the route the previous one returned. The route a chain starts from is the
 * route resolved from the route table, which may be absent when nothing matched. The route the last
 * extension returns is the route Envoy uses.
 *
 * A null route means no route rather than a denied request, so a later extension may still produce
 * one. To force a response an extension returns a route with a direct response. An extension that
 * wraps the input in a DelegatingRoute must first check that it has a route entry, since a direct
 * response or redirect route has none. See source/common/router/delegating_route_impl.h.
 *
 * Implementations are shared by all worker threads, so they must be thread safe and hold no per
 * request state.
 */
class RouteExtension {
public:
  virtual ~RouteExtension() = default;

  /**
   * Customize the route for a request.
   *
   * @param route the route the previous extension returned, or the resolved route for the first
   * extension, or nullptr when no route matched.
   * @param headers the request headers. Header mutations belong in the header transforms of the
   * returned route.
   * @param stream_info the stream info of the downstream request.
   * @param random_value a stable random seed for the request.
   * @return the route to hand to the next extension, which may be the input route, a different
   * route or nullptr for no route.
   */
  virtual RouteConstSharedPtr onRoute(RouteConstSharedPtr route,
                                      const Http::RequestHeaderMap& headers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      uint64_t random_value) const PURE;

  /**
   * Validate that the clusters an extension may select for a request are known to the cluster
   * manager. Called at configuration load when validate_clusters is enabled, so an unknown cluster
   * is rejected before the configuration is accepted. The default validates nothing, so an
   * extension overrides it only when it references clusters.
   *
   * @param cluster_manager the cluster manager to look the clusters up in.
   * @return an error naming the first unknown cluster, ok when every cluster is known.
   */
  virtual absl::Status validateClusters(const Upstream::ClusterManager&) const {
    return absl::OkStatus();
  }
};

using RouteExtensionSharedPtr = std::shared_ptr<const RouteExtension>;

/**
 * Factory for a route extension.
 */
class RouteExtensionFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Create a route extension from the extension config.
   *
   * @param config the typed configuration of the extension.
   * @param context the server factory context, which outlives the route configuration.
   * @return the route extension, or an error when the configuration is invalid.
   */
  virtual absl::StatusOr<RouteExtensionSharedPtr>
  createRouteExtension(const Protobuf::Message& config,
                       Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.router.route_extension"; }
};

} // namespace Router
} // namespace Envoy

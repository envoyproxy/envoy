#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/init/manager.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Router {

/**
 * Produces the resolved route for a request. Implemented by the built-in route matching actions and
 * by route provider extensions, so the route selection dispatch stays a single virtual call and the
 * core does not depend on any extension.
 */
class RouteProducer {
public:
  virtual ~RouteProducer() = default;

  /**
   * Produce the route for this request.
   *
   * @param cb the callback that lets the caller accept or reject a matched route.
   * @param headers the request headers.
   * @param stream_info the stream info of the downstream request.
   * @param random_value the random seed to use when a runtime choice is required.
   * @return the resolved route, or nullptr when nothing matches.
   */
  virtual RouteConstSharedPtr produceRoute(const RouteCallback& cb,
                                           const Http::RequestHeaderMap& headers,
                                           const StreamInfo::StreamInfo& stream_info,
                                           uint64_t random_value) const PURE;
};

using RouteProducerSharedPtr = std::shared_ptr<const RouteProducer>;

/**
 * Extension configuration for a route provider factory. A route provider owns route selection for a
 * virtual host and applies per-request overrides onto the selected route template.
 */
class RouteProviderFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Create a route provider from the resolver config and the route templates it selects among.
   *
   * @param config the resolver configuration for this route provider.
   * @param route_templates the route templates the provider selects among, built once at
   * configuration time.
   * @param context the server factory context.
   * @param init_manager the init manager of the owning route configuration, used to build the
   * per-route configuration overrides at configuration time.
   * @return the route provider, or an error when creation fails.
   */
  virtual absl::StatusOr<RouteProducerSharedPtr>
  createRouteProvider(const Protobuf::Message& config,
                      const std::vector<RouteEntryAndRouteConstSharedPtr>& route_templates,
                      Server::Configuration::ServerFactoryContext& context,
                      Init::Manager& init_manager) PURE;

  std::string category() const override { return "envoy.router.route_provider"; }
};

} // namespace Router
} // namespace Envoy

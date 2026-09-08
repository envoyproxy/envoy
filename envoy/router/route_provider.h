#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Router {

/**
 * The instruction a resolver returns for a request. The core turns this into a route by selecting
 * one of the configured templates. The resolver owns the matching decision and never builds a route
 * itself.
 */
struct RouteDecision {
  enum class Kind {
    // Use the template named by `template_id`.
    SelectTemplate,
    // Use the template named by the provider's `default_template_id`.
    UseDefault,
    // Decline and let route selection continue with `routes` or `matcher`.
    ContinueMatching,
  };

  Kind kind{Kind::ContinueMatching};
  // The selected template id. Only read when `kind` is `SelectTemplate`.
  std::string template_id;
};

/**
 * Resolves the route for a request by selecting one of the configured route templates. Implemented
 * by route provider extensions so the core owns the templates and the shadow comparison while the
 * extension owns the matching decision.
 */
class DynamicRouteResolver {
public:
  virtual ~DynamicRouteResolver() = default;

  /**
   * Select a template for the request.
   *
   * @param headers the request headers.
   * @param stream_info the stream info of the downstream request.
   * @param random_value the random seed to use when a runtime choice is required.
   * @return the selection instruction for the request.
   */
  virtual RouteDecision resolve(const Http::RequestHeaderMap& headers,
                                const StreamInfo::StreamInfo& stream_info,
                                uint64_t random_value) const PURE;

  /**
   * Report the provider and baseline routes when the provider runs in shadow mode. The default is a
   * no-op so resolvers that do not compare need not implement it.
   *
   * @param provider_route the route the provider produced, or nullptr when it produced none.
   * @param baseline_route the route the `routes` or `matcher` baseline produced, or nullptr when
   * none.
   * @param headers the request headers.
   * @param stream_info the stream info of the downstream request.
   */
  virtual void onShadowResult(RouteConstSharedPtr, RouteConstSharedPtr,
                              const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&) const {}
};

using DynamicRouteResolverSharedPtr = std::shared_ptr<const DynamicRouteResolver>;

/**
 * Extension configuration for a route provider resolver factory. A route provider owns route
 * selection for a virtual host and selects among the templates defined next to it.
 */
class RouteProviderFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Create a resolver from the resolver config.
   *
   * @param config the resolver configuration for this route provider.
   * @param context the server factory context.
   * @return the resolver, or an error when creation fails.
   */
  virtual absl::StatusOr<DynamicRouteResolverSharedPtr>
  createRouteResolver(const Protobuf::Message& config,
                      Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.router.route_provider"; }
};

} // namespace Router
} // namespace Envoy

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
 * Whether the extension chain should carry on after an extension has run.
 */
enum class OnRouteResultStatus {
  // Hand the result to the next extension.
  Continue,
  // The result is final. No further extension runs, at this level or any later one.
  StopIteration,
};

/**
 * What an extension made of the route it was given.
 */
struct OnRouteResult {
  // The route to use from here on. May be nullptr.
  RouteConstSharedPtr route;
  // Whether the chain carries on. Defaults to Continue, so an extension only has to say something
  // when it wants its result to be the final one.
  OnRouteResultStatus status{OnRouteResultStatus::Continue};
};

/**
 * A route extension processes the route that route matching resolved for a request and returns the
 * route that should be used instead. It acts on the route, not on the request, and is invoked even
 * when matching resolved no route at all - there is simply no route yet for it to act on.
 *
 * Extensions are executed in order, and the output of each is the input of the next. The route
 * that comes out of the last one is the route Envoy uses for the request, unless an extension ends
 * the chain early by returning OnRouteResultStatus::StopIteration, which makes its own result the
 * final one.
 *
 * Extensions are configured at three levels, which are evaluated in this order, each level's list
 * in configuration order:
 *   1. RouteConfiguration::route_extensions
 *   2. VirtualHost::route_extensions of the matched virtual host
 *   3. Route::route_extensions of the matched route
 * A level is only reached once it has been matched, so a request matching no virtual host
 * evaluates the first level alone.
 *
 * The route an extension takes comes from route matching or from the previous extension, and may
 * be nullptr: an extension may generate a valid route from its own logic, or drop the valid route
 * it was given. Only if the last extension returns nullptr is the request treated as having no
 * route.
 *
 * Which extensions apply, and in which order, is entirely owned by the route configuration
 * implementation; nothing about the chains is visible on the Route, VirtualHost or Config
 * interfaces.
 *
 * Implementations must be thread safe as a single instance is shared by all worker threads, and
 * must not retain any per-request state. The usual way to implement one is to return a
 * DelegatingRoute (see source/common/router/delegating_route_impl.h) wrapping the input route
 * with a small number of methods overridden.
 */
class RouteExtension {
public:
  virtual ~RouteExtension() = default;

  /**
   * Process a matched route.
   *
   * @param route the route produced by the previous extension in the chain, or the originally
   *        matched route for the first extension. May be nullptr: the chains also run when route
   *        matching found nothing, so that an extension can supply a fallback route, and an
   *        earlier extension may have dropped the route. An extension that only refines an
   *        existing route should return @param route unchanged when it is nullptr.
   * @param headers the request headers. Extensions must not modify them; header mutations belong
   *        in the route's own header transforms so that they are applied at the right point of
   *        the request lifetime.
   * @param info the stream info of the downstream request.
   * @param random a stable random seed for the request, for extensions that need to make a
   *        weighted choice.
   * @return the route to hand to the next extension in the chain, which may be @param route
   *         itself if the extension does not want to change anything, or nullptr to drop it,
   *         together with whether the chain should carry on. Returning
   *         OnRouteResultStatus::StopIteration makes the returned route the final one: no later
   *         extension runs, at this level or any of the levels after it. If the route that comes
   *         out of the chain is nullptr the request is handled as if no route had matched.
   */
  virtual OnRouteResult onRoute(RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
                                const StreamInfo::StreamInfo& info, uint64_t random) const PURE;
};

using RouteExtensionSharedPtr = std::shared_ptr<const RouteExtension>;

/**
 * Extension configuration for route extension factory.
 */
class RouteExtensionFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Create a particular route extension implementation.
   *
   * @param config the typed configuration of the extension.
   * @param context the server factory context. The extension may keep a reference to it as it
   *        outlives the route configuration. Use context.messageValidationVisitor() if any nested
   *        configuration needs to be validated.
   * @return the route extension, or an error status if the configuration is invalid.
   */
  virtual absl::StatusOr<RouteExtensionSharedPtr>
  createRouteExtension(const Protobuf::Message& config,
                       Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.router.route_extensions"; }
};

} // namespace Router
} // namespace Envoy

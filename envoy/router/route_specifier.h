#pragma once

#include <memory>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Router {

/**
 * Whether the specifier chain should carry on after a specifier has run.
 */
enum class OnRouteResultStatus {
  // Hand the result to the next specifier.
  Continue,
  // The result is final. No further specifier runs, at this level or any later one.
  StopIteration,
};

/**
 * What a specifier made of the route it was given.
 */
struct OnRouteResult {
  // The route to use from here on. May be nullptr.
  RouteConstSharedPtr route;
  // Whether the chain carries on. Defaults to Continue, so a specifier only has to say something
  // when it wants its result to be the final one.
  OnRouteResultStatus status{OnRouteResultStatus::Continue};
};

/**
 * A route specifier processes the route that route matching resolved for a request and returns the
 * route that should be used instead. It acts on the route, not on the request, and is invoked even
 * when matching resolved no route at all - there is simply no route yet for it to act on.
 *
 * Specifiers are executed in order, and the output of each is the input of the next. The route
 * that comes out of the last one is the route Envoy uses for the request, unless a specifier ends
 * the chain early by returning OnRouteResultStatus::StopIteration, which makes its own result the
 * final one.
 *
 * Specifiers are configured at three levels, which are evaluated in this order, each level's list
 * in configuration order:
 *   1. RouteConfiguration::route_specifiers
 *   2. VirtualHost::route_specifiers of the matched virtual host
 *   3. Route::route_specifiers of the matched route
 * A level is only reached once it has been matched, so a request matching no virtual host
 * evaluates the first level alone.
 *
 * The route a specifier takes comes from route matching or from the previous specifier, and may
 * be nullptr: a specifier may generate a valid route from its own logic, or drop the valid route
 * it was given. Only if the last specifier returns nullptr is the request treated as having no
 * route.
 *
 * Which specifiers apply, and in which order, is entirely owned by the route configuration
 * implementation; nothing about the chains is visible on the Route, VirtualHost or Config
 * interfaces.
 *
 * Implementations must be thread safe as a single instance is shared by all worker threads, and
 * must not retain any per-request state. The usual way to implement one is to return a
 * DelegatingRoute (see source/common/router/delegating_route_impl.h) wrapping the input route
 * with a small number of methods overridden.
 */
class RouteSpecifier {
public:
  virtual ~RouteSpecifier() = default;

  /**
   * Process a matched route.
   *
   * @param route the route produced by the previous specifier in the chain, or the originally
   *        matched route for the first specifier. May be nullptr: the chains also run when route
   *        matching found nothing, so that a specifier can supply a fallback route, and an
   *        earlier specifier may have dropped the route. A specifier that only refines an
   *        existing route should return @param route unchanged when it is nullptr.
   * @param headers the request headers. Specifiers must not modify them; header mutations belong
   *        in the route's own header transforms so that they are applied at the right point of
   *        the request lifetime.
   * @param info the stream info of the downstream request.
   * @param random a stable random seed for the request, for specifiers that need to make a
   *        weighted choice.
   * @return the route to hand to the next specifier in the chain, which may be @param route
   *         itself if the specifier does not want to change anything, or nullptr to drop it,
   *         together with whether the chain should carry on. Returning
   *         OnRouteResultStatus::StopIteration makes the returned route the final one: no later
   *         specifier runs, at this level or any of the levels after it. If the route that comes
   *         out of the chain is nullptr the request is handled as if no route had matched.
   */
  virtual OnRouteResult onRoute(RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
                                const StreamInfo::StreamInfo& info, uint64_t random) const PURE;
};

using RouteSpecifierSharedPtr = std::shared_ptr<const RouteSpecifier>;

/**
 * A route built by a RouteBuilder, which a specifier evaluates against a request the same way route
 * matching evaluates a configured route. Implementations must be thread safe.
 */
class MatchableRoute {
public:
  virtual ~MatchableRoute() = default;

  /**
   * Evaluate the route against a request.
   *
   * @param headers the request headers.
   * @param stream_info the stream info of the downstream request.
   * @param random a stable random seed for the request, used when the route requires a runtime
   *        choice.
   * @return the route to use for the request, or nullptr when the request does not match. The
   *         returned route resolves weighted clusters and cluster specifier plugins, so it may
   *         differ from the route that was built.
   */
  virtual RouteConstSharedPtr match(const Http::RequestHeaderMap& headers,
                                    const StreamInfo::StreamInfo& stream_info,
                                    uint64_t random) const PURE;
};

using MatchableRouteConstSharedPtr = std::shared_ptr<const MatchableRoute>;

/**
 * Builds routes that belong to the configuration the specifier is configured on, so that a
 * specifier can select one of them for a request instead of restating every route field itself.
 * A built route inherits from its virtual host and route configuration exactly like a configured
 * route does.
 *
 * Only valid for the duration of the createRouteSpecifier() call it was handed to, because it
 * borrows the init manager that the routes it builds warm up with.
 */
class RouteBuilder {
public:
  virtual ~RouteBuilder() = default;

  /**
   * Build a route.
   *
   * @param route the route configuration. `route_specifiers` must be empty, since a built route is
   *        not run through the specifier chains.
   * @param validate_clusters whether the clusters the route names are looked up in the cluster
   *        manager.
   * @return the built route, or an error status if the configuration is invalid.
   */
  virtual absl::StatusOr<MatchableRouteConstSharedPtr>
  build(const envoy::config::route::v3::Route& route, bool validate_clusters) PURE;
};

/**
 * Context handed to a route specifier factory. Only valid for the duration of the
 * createRouteSpecifier() call.
 */
class RouteSpecifierFactoryContext {
public:
  virtual ~RouteSpecifierFactoryContext() = default;

  /**
   * @return the server factory context. The specifier may keep a reference to it as it outlives the
   *         route configuration. Use serverFactoryContext().messageValidationVisitor() if any
   *         nested configuration needs to be validated.
   */
  virtual Server::Configuration::ServerFactoryContext& serverFactoryContext() PURE;

  /**
   * @return the builder for routes of the virtual host the specifier is configured on, or an empty
   *         reference for a specifier configured on a route configuration, which has no virtual
   *         host to build routes in.
   */
  virtual OptRef<RouteBuilder> routeBuilder() PURE;
};

/**
 * Extension configuration for route specifier factory.
 */
class RouteSpecifierFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Create a particular route specifier implementation.
   *
   * @param config the typed configuration of the specifier.
   * @param context the factory context. Only valid for the duration of this call, although the
   *        server factory context it exposes may be kept.
   * @return the route specifier, or an error status if the configuration is invalid.
   */
  virtual absl::StatusOr<RouteSpecifierSharedPtr>
  createRouteSpecifier(const Protobuf::Message& config, RouteSpecifierFactoryContext& context) PURE;

  std::string category() const override { return "envoy.router.route_specifiers"; }
};

} // namespace Router
} // namespace Envoy

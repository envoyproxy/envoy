#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/router/route_specifier.h"
#include "envoy/server/factory_context.h"

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Router {

// Owning storage for one level of the route specifier chain (route configuration, virtual host or
// route). Two inline slots because the common case is one or two specifiers per level.
using RouteSpecifierList = absl::InlinedVector<RouteSpecifierSharedPtr, 2>;

// A borrowed view of one level of the chain. Always owned by the route configuration or by one of
// the route entries it holds, both of which outlive any request being routed through them.
using RouteSpecifierSpan = absl::Span<const RouteSpecifierSharedPtr>;

/**
 * Context handed to route specifier factories.
 */
class RouteSpecifierFactoryContextImpl : public RouteSpecifierFactoryContext {
public:
  RouteSpecifierFactoryContextImpl(Server::Configuration::ServerFactoryContext& factory_context,
                                   OptRef<RouteBuilder> route_builder)
      : factory_context_(factory_context), route_builder_(route_builder) {}

  // Router::RouteSpecifierFactoryContext
  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return factory_context_;
  }
  OptRef<RouteBuilder> routeBuilder() override { return route_builder_; }

private:
  Server::Configuration::ServerFactoryContext& factory_context_;
  const OptRef<RouteBuilder> route_builder_;
};

/**
 * Create the route specifiers of one configuration level.
 *
 * @param configs the repeated `route_specifiers` field of a RouteConfiguration, VirtualHost or
 *        Route.
 * @param context the factory context. The message validation visitor of its server factory context
 *        is used to translate the specifier configurations.
 * @return the created specifiers in configuration order, or an error status if any specifier is
 *         unknown or misconfigured.
 */
absl::StatusOr<RouteSpecifierList> createRouteSpecifiers(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& configs,
    RouteSpecifierFactoryContext& context);

/**
 * Run the route specifier chains of all three configuration levels. The levels run in order:
 * route configuration, then virtual host, then route. A nullptr route is a normal value flowing
 * through the chains rather than a stop condition, so a specifier can both drop a matched route
 * and supply one where matching found none. The one thing that does end the chain early is a
 * specifier declaring its result final, which skips every specifier after it, including those of
 * the later levels.
 *
 * @param route the matched route, possibly already a wrapper produced by a cluster specifier
 *        plugin, or nullptr if nothing matched.
 * @param config_specifiers the route configuration level chain, may be empty.
 * @param vhost_specifiers the virtual host level chain, may be empty. Empty when no virtual host
 *        matched the request.
 * @param route_specifiers the route level chain, may be empty. Always empty when @param route is
 *        nullptr, since there is no route to take it from.
 * @param headers the HTTP request headers.
 * @param stream_info the stream information for the request.
 * @param random a random value for use by the specifiers.
 * @return the route to use for the request, @param route itself if every chain is empty, or
 *         nullptr if there is no route for the request.
 */
RouteConstSharedPtr
applyRouteSpecifiers(RouteConstSharedPtr route, RouteSpecifierSpan config_specifiers,
                     RouteSpecifierSpan vhost_specifiers, RouteSpecifierSpan route_specifiers,
                     const Http::RequestHeaderMap& headers,
                     const StreamInfo::StreamInfo& stream_info, uint64_t random);

} // namespace Router
} // namespace Envoy

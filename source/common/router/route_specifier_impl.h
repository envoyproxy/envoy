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
                                   OptRef<RouteBuilder> route_builder, RouteSpecifierLevel level)
      : factory_context_(factory_context), route_builder_(route_builder), level_(level) {}

  // Router::RouteSpecifierFactoryContext
  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return factory_context_;
  }
  OptRef<RouteBuilder> routeBuilder() override { return route_builder_; }
  RouteSpecifierLevel level() const override { return level_; }

private:
  Server::Configuration::ServerFactoryContext& factory_context_;
  const OptRef<RouteBuilder> route_builder_;
  const RouteSpecifierLevel level_;
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
 * Run one route specifier chain, stopping at the first specifier that declares its result final or
 * asks matching to carry on. A nullptr route is not a stop condition, it is passed on to the next
 * specifier, which is free to supply one.
 *
 * @param specifiers the chain to run, may be empty.
 * @param route the route handed to the first specifier, may be nullptr.
 * @param headers the HTTP request headers.
 * @param stream_info the stream information for the request.
 * @param random a random value for use by the specifiers.
 * @return the result of the last specifier that ran, or the input route with a Continue status when
 *         the chain is empty.
 */
OnRouteResult runRouteSpecifiers(RouteSpecifierSpan specifiers, RouteConstSharedPtr route,
                                 const Http::RequestHeaderMap& headers,
                                 const StreamInfo::StreamInfo& stream_info, uint64_t random);

/**
 * Run the route configuration and virtual host chains on the route the match resolved. The route
 * level chain runs during matching, so it is not run here. The chains run even when @param route is
 * nullptr, so a specifier can supply a route where matching found none. continue_matching has no
 * meaning outside the route level chain and is ignored with a debug log.
 *
 * @param route the matched route, possibly already a wrapper produced by a cluster specifier
 *        plugin, or nullptr if nothing matched.
 * @param config_specifiers the route configuration level chain, may be empty.
 * @param vhost_specifiers the virtual host level chain, may be empty. Empty when no virtual host
 *        matched the request.
 * @param headers the HTTP request headers.
 * @param stream_info the stream information for the request.
 * @param random a random value for use by the specifiers.
 * @return the route to use for the request, the input @param route when neither chain changes it,
 *         or nullptr if there is no route for the request.
 */
RouteConstSharedPtr
applyRouteSpecifiers(RouteConstSharedPtr route, RouteSpecifierSpan config_specifiers,
                     RouteSpecifierSpan vhost_specifiers, const Http::RequestHeaderMap& headers,
                     const StreamInfo::StreamInfo& stream_info, uint64_t random);

} // namespace Router
} // namespace Envoy

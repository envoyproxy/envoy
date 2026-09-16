#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/router/route_extension.h"
#include "envoy/server/factory_context.h"

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Router {

// Owning storage for one level of the route extension chain (route configuration, virtual host or
// route). Two inline slots because the common case is one or two extensions per level.
using RouteExtensionList = absl::InlinedVector<RouteExtensionSharedPtr, 2>;

// A borrowed view of one level of the chain. Always owned by the route configuration or by one of
// the route entries it holds, both of which outlive any request being routed through them.
using RouteExtensionSpan = absl::Span<const RouteExtensionSharedPtr>;

/**
 * Create the route extensions of one configuration level.
 *
 * @param configs the repeated `route_extensions` field of a RouteConfiguration, VirtualHost or
 *        Route.
 * @param context the server factory context. Its message validation visitor is used to translate
 *        the extension configurations.
 * @return the created extensions in configuration order, or an error status if any extension is
 *         unknown or misconfigured.
 */
absl::StatusOr<RouteExtensionList> createRouteExtensions(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& configs,
    Server::Configuration::ServerFactoryContext& context);

/**
 * Run the route extension chains of all three configuration levels. The levels run in order:
 * route configuration, then virtual host, then route. A nullptr route is a normal value flowing
 * through the chains rather than a stop condition, so an extension can both drop a matched route
 * and supply one where matching found none. The one thing that does end the chain early is an
 * extension declaring its result final, which skips every extension after it, including those of
 * the later levels.
 *
 * @param route the matched route, possibly already a wrapper produced by a cluster specifier
 *        plugin, or nullptr if nothing matched.
 * @param config_extensions the route configuration level chain, may be empty.
 * @param vhost_extensions the virtual host level chain, may be empty. Empty when no virtual host
 *        matched the request.
 * @param route_extensions the route level chain, may be empty. Always empty when @param route is
 *        nullptr, since there is no route to take it from.
 * @return the route to use for the request, @param route itself if every chain is empty, or
 *         nullptr if there is no route for the request.
 */
RouteConstSharedPtr
applyRouteExtensions(RouteConstSharedPtr route, RouteExtensionSpan config_extensions,
                     RouteExtensionSpan vhost_extensions, RouteExtensionSpan route_extensions,
                     const Http::RequestHeaderMap& headers,
                     const StreamInfo::StreamInfo& stream_info, uint64_t random_value);

} // namespace Router
} // namespace Envoy

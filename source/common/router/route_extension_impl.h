#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/router/route_extension.h"
#include "envoy/server/factory_context.h"

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Router {

// Owning storage for the route extensions of one configuration level. Two inline slots because a
// level usually has one or two extensions.
using RouteExtensionList = absl::InlinedVector<RouteExtensionSharedPtr, 2>;

/**
 * Create the route extensions of one configuration level.
 *
 * @param configs the route_extensions field of a route configuration, virtual host or route.
 * @param context the server factory context.
 * @return the extensions in configuration order, or an error when an extension is unknown or
 * misconfigured.
 */
absl::StatusOr<RouteExtensionList> createRouteExtensions(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& configs,
    Server::Configuration::ServerFactoryContext& context);

/**
 * Run one level of the route extension chain over a route.
 *
 * @param extensions the extensions of the level, which may be empty.
 * @param route the route the previous level produced, which may be nullptr when no route matched.
 * @param headers the request headers.
 * @param stream_info the stream info of the downstream request.
 * @param random_value a stable random seed for the request.
 * @return the route the last extension of the level returned, or the input route when the level is
 * empty.
 */
RouteConstSharedPtr runRouteExtensions(const RouteExtensionList& extensions,
                                       RouteConstSharedPtr route,
                                       const Http::RequestHeaderMap& headers,
                                       const StreamInfo::StreamInfo& stream_info,
                                       uint64_t random_value);

/**
 * Validate the clusters every route extension of one level may select against the cluster manager.
 * Called at configuration load when validate_clusters is enabled.
 *
 * @param extensions the extensions of the level, which may be empty.
 * @param cluster_manager the cluster manager to look the clusters up in.
 * @return an error naming the first unknown cluster, ok when every cluster is known.
 */
absl::Status validateRouteExtensionClusters(const RouteExtensionList& extensions,
                                            const Upstream::ClusterManager& cluster_manager);

} // namespace Router
} // namespace Envoy

#pragma once

// NOLINT(namespace-envoy)

/**
 * Template configuration compiled with the Envoy Mobile library.
 * More information about Envoy's config can be found at:
 * https://www.envoyproxy.io/docs/envoy/latest/configuration/configuration
 */
extern const char* config_template;

/**
 * Template configuration used for dynamic creation of the platform-bridged filter chain.
 */
extern const char* platform_filter_template;

/**
 * Template configuration used for dynamic creation of the native filter chain.
 */
extern const char* native_filter_template;

/**
 * Template that enables the route cache reset filter in the chain.
 * Should only be added when the route cache should be cleared on every request
 * going through the filter chain between initial route resolution and the router
 * filter's invocation on the request path. Typically only used for enabling
 * direct responses to mutate headers which are then later used for routing.
 */
extern const char* route_cache_reset_filter_template;

/**
 * Template configuration used for creating "fake" remote clusters which enable
 * local responses to be returned via direct response configurations.
 */
extern const char* fake_remote_cluster_template;

/**
 * Template configuration used for creating "fake" remote listeners which enable
 * local responses to be returned via direct response configurations.
 */
extern const char* fake_remote_listener_template;

/**
 * Template used for setting up the gRPC stats sink.
 */
extern const char* grpc_stats_sink_template;

/**
 * Template used for setting up the statsd stats sink.
 */
extern const char* statsd_sink_template;

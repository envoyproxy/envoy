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
 * Number of spaces to indent custom cluster entries.
 */
extern const int custom_cluster_indent;

/**
 * Number of spaces to indent custom listener entries.
 */
extern const int custom_listener_indent;

/**
 * Number of spaces to indent custom filter entries.
 */
extern const int custom_filter_indent;

/**
 * Number of spaces to indent custom route entries.
 */
extern const int custom_route_indent;

/**
 * Number of spaces to indent response entries for the (test-only) fake remote listener.
 */
extern const int fake_remote_response_indent;

/**
 * Test-only fake remote listener config insert.
 */
extern const char* fake_remote_listener_insert;

/**
 * Test-only fake remote cluster config insert.
 */
extern const char* fake_remote_cluster_insert;

/**
 * Test-only fake remote route config insert.
 */
extern const char* fake_remote_route_insert;

/**
 * Insert that enables the alternate protocols cache filter in the filter chain.
 * This is only needed for (currently experimental) QUIC/H3 support.
 */
extern const char* alternate_protocols_cache_filter_insert;

/**
 * Insert that enables the route cache reset filter in the filter chain.
 * Should only be added when the route cache should be cleared on every request
 * going through the filter chain between initial route resolution and the router
 * filter's invocation on the request path. Typically only used for enabling
 * direct responses to mutate headers which are then later used for routing.
 */
extern const char* route_cache_reset_filter_insert;

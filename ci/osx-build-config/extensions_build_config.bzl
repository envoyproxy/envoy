# Should contain https://github.com/envoyproxy/envoy-mobile/blob/main/envoy_build_config/extensions_build_config.bzl
# plus a few commonly used filters to verify they compile.
EXTENSIONS = {
    # Mobile
    "envoy.clusters.dynamic_forward_proxy": "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.clusters.static": "//source/extensions/clusters/static:static_cluster_lib",
    "envoy.filters.connection_pools.http.generic": "//source/extensions/upstreams/http/generic:config",
    "envoy.filters.http.alternate_protocols_cache": "//source/extensions/filters/http/alternate_protocols_cache:config",
    "envoy.filters.http.buffer": "//source/extensions/filters/http/buffer:config",
    "envoy.filters.http.decompressor": "//source/extensions/filters/http/decompressor:config",
    "envoy.filters.http.dynamic_forward_proxy": "//source/extensions/filters/http/dynamic_forward_proxy:config",
    "envoy.filters.http.router": "//source/extensions/filters/http/router:config",
    "envoy.filters.network.http_connection_manager": "//source/extensions/filters/network/http_connection_manager:config",
    "envoy.http.original_ip_detection.xff": "//source/extensions/http/original_ip_detection/xff:config",
    "envoy.network.dns_resolver.apple": "//source/extensions/network/dns_resolver/apple:config",
    "envoy.network.dns_resolver.getaddrinfo": "//source/extensions/network/dns_resolver/getaddrinfo:config",
    "envoy.transport_sockets.http_11_proxy": "//source/extensions/transport_sockets/http_11_proxy:upstream_config",
    "envoy.transport_sockets.raw_buffer": "//source/extensions/transport_sockets/raw_buffer:config",
    "envoy.transport_sockets.tls": "//source/extensions/transport_sockets/tls:config",
    "envoy.http.stateful_header_formatters.preserve_case": "//source/extensions/http/header_formatters/preserve_case:config",
    "envoy.load_balancing_policies.round_robin": "//source/extensions/load_balancing_policies/round_robin:config",
    "envoy.load_balancing_policies.cluster_provided": "//source/extensions/load_balancing_policies/cluster_provided:config",

    # Extras
    "envoy.filters.http.ext_proc": "//source/extensions/filters/http/ext_proc:config",
    "envoy.filters.http.rbac": "//source/extensions/filters/http/rbac:config",
    "envoy.filters.http.ext_authz": "//source/extensions/filters/http/ext_authz:config",
    "envoy.filters.http.lua": "//source/extensions/filters/http/lua:config",
    "envoy.filters.network.rbac": "//source/extensions/filters/network/rbac:config",
    "envoy.clusters.eds": "//source/extensions/clusters/eds:eds_lib",
    "envoy.access_loggers.file": "//source/extensions/access_loggers/file:config",
    "envoy.formatter.cel": "//source/extensions/formatter/cel:config",
    "envoy.filters.network.redis_proxy": "//source/extensions/filters/network/redis_proxy:config",
}
WINDOWS_EXTENSIONS = {}
EXTENSION_CONFIG_VISIBILITY = ["//:extension_config"]
EXTENSION_PACKAGE_VISIBILITY = ["//:extension_library"]
CONTRIB_EXTENSION_PACKAGE_VISIBILITY = ["//:contrib_library"]
MOBILE_PACKAGE_VISIBILITY = ["//:mobile_library"]

# As part of (https://github.com/envoyproxy/envoy-mobile/issues/175) we turned down alwayslink for envoy libraries
# This tracks libraries that should be registered as extensions.
LEGACY_ALWAYSLINK = 1

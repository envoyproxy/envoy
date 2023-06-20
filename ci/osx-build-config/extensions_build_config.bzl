# Should match https://github.com/envoyproxy/envoy-mobile/blob/main/envoy_build_config/extensions_build_config.bzl
# TODO(mattklein123): Actually pull this file from that repo and remove the envoy mobile specific filters.
EXTENSIONS = {
    "envoy.clusters.dynamic_forward_proxy": "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.filters.connection_pools.http.generic": "//source/extensions/upstreams/http/generic:config",
    "envoy.filters.http.buffer": "//source/extensions/filters/http/buffer:config",
    "envoy.filters.http.dynamic_forward_proxy": "//source/extensions/filters/http/dynamic_forward_proxy:config",
    "envoy.filters.http.router": "//source/extensions/filters/http/router:config",
    "envoy.filters.network.http_connection_manager": "//source/extensions/filters/network/http_connection_manager:config",
    "envoy.stat_sinks.metrics_service": "//source/extensions/stat_sinks/metrics_service:config",
    "envoy.transport_sockets.raw_buffer": "//source/extensions/transport_sockets/raw_buffer:config",
    "envoy.transport_sockets.tls": "//source/extensions/transport_sockets/tls:config",
    "envoy.network.dns_resolver.cares": "//source/extensions/network/dns_resolver/cares:config",
    "envoy.network.dns_resolver.apple": "//source/extensions/network/dns_resolver/apple:config",
}
WINDOWS_EXTENSIONS = {}
EXTENSION_CONFIG_VISIBILITY = ["//:extension_config"]
EXTENSION_PACKAGE_VISIBILITY = ["//:extension_library"]
CONTRIB_EXTENSION_PACKAGE_VISIBILITY = ["//:contrib_library"]
MOBILE_PACKAGE_VISIBILITY = ["//:mobile_library"]

# As part of (https://github.com/envoyproxy/envoy-mobile/issues/175) we turned down alwayslink for envoy libraries
# This tracks libraries that should be registered as extensions.
LEGACY_ALWAYSLINK = 1

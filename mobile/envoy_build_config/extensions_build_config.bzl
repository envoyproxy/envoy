CONTRIB_EXTENSION_PACKAGE_VISIBILITY = ["@envoy//:contrib_library"]
MOBILE_PACKAGE_VISIBILITY = ["@envoy//:mobile_library"]
EXTENSION_CONFIG_VISIBILITY = ["//visibility:public"]
EXTENSION_PACKAGE_VISIBILITY = ["//visibility:public"]
EXTENSIONS = {
    "envoy.clusters.dynamic_forward_proxy":                "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.clusters.static":                               "//source/extensions/clusters/static:static_cluster_lib",
    "envoy.filters.connection_pools.http.generic":         "//source/extensions/upstreams/http/generic:config",
    "envoy.filters.http.alternate_protocols_cache":        "//source/extensions/filters/http/alternate_protocols_cache:config",
    "envoy.filters.http.assertion":                        "@envoy_mobile//library/common/extensions/filters/http/assertion:config",
    "envoy.filters.http.buffer":                           "//source/extensions/filters/http/buffer:config",
    "envoy.filters.http.decompressor":                     "//source/extensions/filters/http/decompressor:config",
    "envoy.filters.http.dynamic_forward_proxy":            "//source/extensions/filters/http/dynamic_forward_proxy:config",
    "envoy.filters.http.local_error":                      "@envoy_mobile//library/common/extensions/filters/http/local_error:config",
    "envoy.filters.http.platform_bridge":                  "@envoy_mobile//library/common/extensions/filters/http/platform_bridge:config",
    "envoy.filters.http.network_configuration":            "@envoy_mobile//library/common/extensions/filters/http/network_configuration:config",
    "envoy.filters.http.route_cache_reset":                "@envoy_mobile//library/common/extensions/filters/http/route_cache_reset:config",
    "envoy.filters.http.router":                           "//source/extensions/filters/http/router:config",
    "envoy.filters.network.http_connection_manager":       "//source/extensions/filters/network/http_connection_manager:config",
    "envoy.http.original_ip_detection.xff":                "//source/extensions/http/original_ip_detection/xff:config",
    "envoy.key_value.platform":                            "@envoy_mobile//library/common/extensions/key_value/platform:config",
    "envoy.network.dns_resolver.apple":                    "//source/extensions/network/dns_resolver/apple:config",
    "envoy.network.dns_resolver.getaddrinfo":              "//source/extensions/network/dns_resolver/getaddrinfo:config",
    "envoy.retry.options.network_configuration":           "@envoy_mobile//library/common/extensions/retry/options/network_configuration:config",
    "envoy.transport_sockets.http_11_proxy":               "//source/extensions/transport_sockets/http_11_proxy:upstream_config",
    "envoy.transport_sockets.raw_buffer":                  "//source/extensions/transport_sockets/raw_buffer:config",
    "envoy.transport_sockets.tls":                         "//source/extensions/transport_sockets/tls:upstream_config",
    "envoy.http.stateful_header_formatters.preserve_case": "//source/extensions/http/header_formatters/preserve_case:config",
    "envoy_mobile.cert_validator.platform_bridge_cert_validator": "@envoy_mobile//library/common/extensions/cert_validator/platform_bridge:config",
    "envoy.listener_manager_impl.api":                     "@envoy_mobile//library/common/extensions/listener_managers/api_listener_manager:api_listener_manager_lib",
    "envoy.connection_handler.default":                    "//source/extensions/listener_managers/listener_manager:connection_handler_lib",
    "envoy.load_balancing_policies.round_robin":           "//source/extensions/load_balancing_policies/round_robin:config",
    "envoy.load_balancing_policies.cluster_provided":      "//source/extensions/load_balancing_policies/cluster_provided:config",
}
WINDOWS_EXTENSIONS = {}
LEGACY_ALWAYSLINK = 1

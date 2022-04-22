CONTRIB_EXTENSION_PACKAGE_VISIBILITY = ["@envoy//:contrib_library"]
EXTENSION_CONFIG_VISIBILITY = ["//visibility:public"]
EXTENSION_PACKAGE_VISIBILITY = ["//visibility:public"]
EXTENSIONS = {
    "envoy.clusters.dynamic_forward_proxy":                "//source/extensions/clusters/dynamic_forward_proxy:cluster",
    "envoy.filters.connection_pools.http.generic":         "//source/extensions/upstreams/http/generic:config",
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
    "envoy.retry.options.network_configuration":           "@envoy_mobile//library/common/extensions/retry/options/network_configuration:config",
    "envoy.stat_sinks.metrics_service":                    "//source/extensions/stat_sinks/metrics_service:config",
    "envoy.transport_sockets.raw_buffer":                  "//source/extensions/transport_sockets/raw_buffer:config",
    "envoy.transport_sockets.tls":                         "//source/extensions/transport_sockets/tls:config",
    "envoy.http.stateful_header_formatters.preserve_case": "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
}
WINDOWS_EXTENSIONS = {}

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "extension_registry",
    srcs = [
        "extension_registry.cc",
    ],
    hdrs = ["extension_registry.h"],
    repository = "@envoy",
    deps = [
        "extension_registry_platform_additions",
        "@envoy//source/common/network:socket_lib",
        "@envoy//source/common/router:upstream_codec_filter_lib",
        "@envoy//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "@envoy//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "@envoy//source/extensions/clusters/static:static_cluster_lib",
        "@envoy//source/extensions/compression/brotli/decompressor:config",
        "@envoy//source/extensions/compression/gzip/decompressor:config",
        "@envoy//source/extensions/filters/http/alternate_protocols_cache:config",
        "@envoy//source/extensions/filters/http/buffer:config",
        "@envoy//source/extensions/filters/http/decompressor:config",
        "@envoy//source/extensions/filters/http/dynamic_forward_proxy:config",
        "@envoy//source/extensions/filters/http/router:config",
        "@envoy//source/extensions/filters/network/http_connection_manager:config",
        "@envoy//source/extensions/http/header_formatters/preserve_case:config",
        "@envoy//source/extensions/network/dns_resolver/getaddrinfo:config",
        "@envoy//source/extensions/stat_sinks/metrics_service:config",
        "@envoy//source/extensions/stat_sinks/statsd:config",
        "@envoy//source/extensions/transport_sockets/http_11_proxy:upstream_config",
        "@envoy//source/extensions/transport_sockets/raw_buffer:config",
        "@envoy//source/extensions/transport_sockets/tls:config",
        "@envoy//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
        "@envoy//source/extensions/upstreams/http/generic:config",
        "@envoy_mobile//library/common/extensions/cert_validator/platform_bridge:config",
        "@envoy_mobile//library/common/extensions/filters/http/assertion:config",
        "@envoy_mobile//library/common/extensions/filters/http/local_error:config",
        "@envoy_mobile//library/common/extensions/filters/http/network_configuration:config",
        "@envoy_mobile//library/common/extensions/filters/http/platform_bridge:config",
        "@envoy_mobile//library/common/extensions/filters/http/route_cache_reset:config",
        "@envoy_mobile//library/common/extensions/filters/http/socket_tag:config",
        "@envoy_mobile//library/common/extensions/retry/options/network_configuration:config",
    ],
)

envoy_cc_library(
    name = "extension_registry_platform_additions",
    srcs = select({
        "@envoy//bazel:apple": ["extension_registry_apple.cc"],
        "//conditions:default": ["extension_registry_noop.cc"],
    }),
    hdrs = ["extension_registry_platform_additions.h"],
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:apple": ["@envoy//source/extensions/network/dns_resolver/apple:config"],
        "//conditions:default": [],
    }),
)

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
        "@envoy//source/common/network:socket_lib",
        "@envoy//source/common/upstream:logical_dns_cluster_lib",
        "@envoy//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "@envoy//source/extensions/compression/gzip/decompressor:config",
        "@envoy//source/extensions/filters/http/buffer:config",
        "@envoy//source/extensions/filters/http/decompressor:config",
        "@envoy//source/extensions/filters/http/dynamic_forward_proxy:config",
        "@envoy//source/extensions/filters/http/router:config",
        "@envoy//source/extensions/filters/network/http_connection_manager:config",
        "@envoy//source/extensions/stat_sinks/metrics_service:config",
        "@envoy//source/extensions/stat_sinks/statsd:config",
        "@envoy//source/extensions/transport_sockets/raw_buffer:config",
        "@envoy//source/extensions/transport_sockets/tls:config",
        "@envoy//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
        "@envoy//source/extensions/upstreams/http/generic:config",
        "@envoy_mobile//library/common/extensions/filters/http/assertion:config",
        "@envoy_mobile//library/common/extensions/filters/http/local_error:config",
        "@envoy_mobile//library/common/extensions/filters/http/platform_bridge:config",
        "@envoy_mobile//library/common/extensions/filters/http/route_cache_reset:config",
        "@envoy_mobile//library/common/extensions/filters/http/test_accessor:config",
    ],
)

licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "registry",
    srcs = [
        "registry.cc",
    ],
    hdrs = ["registry.h"],
    repository = "@envoy",
    deps = [
        "@envoy//source/common/upstream:logical_dns_cluster_lib",
        "@envoy//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "@envoy//source/extensions/filters/http/dynamic_forward_proxy:config",
        "@envoy//source/extensions/filters/http/router:config",
        "@envoy//source/extensions/filters/network/http_connection_manager:config",
        "@envoy//source/extensions/stat_sinks/metrics_service:config",
        "@envoy//source/extensions/transport_sockets/tls:config",
    ],
)

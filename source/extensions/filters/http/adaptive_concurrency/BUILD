load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that dynamically adjusts the number of allowed concurrent
# requests based on sampled latencies.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/adaptive_concurrency_filter

envoy_extension_package()

envoy_cc_library(
    name = "adaptive_concurrency_filter_lib",
    srcs = ["adaptive_concurrency_filter.cc"],
    hdrs = ["adaptive_concurrency_filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//source/common/runtime:runtime_protos_lib",
        "//source/extensions/filters/http/adaptive_concurrency/controller:controller_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/adaptive_concurrency/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/adaptive_concurrency:adaptive_concurrency_filter_lib",
        "//source/extensions/filters/http/adaptive_concurrency/controller:controller_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/adaptive_concurrency/v3:pkg_cc_proto",
    ],
)

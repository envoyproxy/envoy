licenses(["notice"])  # Apache 2

# HTTP L7 filter that dynamically adjusts the number of allowed concurrent
# requests based on sampled latencies.
# Public docs: docs/root/configuration/http_filters/adaptive_concurrency_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "adaptive_concurrency_filter_lib",
    srcs = ["adaptive_concurrency_filter.cc"],
    hdrs = ["adaptive_concurrency_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/adaptive_concurrency/concurrency_controller:concurrency_controller_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/config/filter/http/adaptive_concurrency/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/adaptive_concurrency:adaptive_concurrency_filter_lib",
        "//source/extensions/filters/http/adaptive_concurrency/concurrency_controller:concurrency_controller_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that dynamically adjusts the number of allowed concurrent
# requests based on sampled latencies.
# Public docs: TODO (tonya11en)

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
        "@envoy_api//envoy/config/filter/http/adaptive_concurrency/v2alpha:adaptive_concurrency_cc",
    ],
)

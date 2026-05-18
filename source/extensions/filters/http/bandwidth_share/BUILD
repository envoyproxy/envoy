load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Local BandwidthShare HTTP L7 filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/bandwidth_share_filter

envoy_extension_package()

envoy_cc_library(
    name = "fair_token_bucket_impl_lib",
    srcs = ["fair_token_bucket_impl.cc"],
    hdrs = ["fair_token_bucket_impl.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/common:token_bucket_interface",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
    ],
)

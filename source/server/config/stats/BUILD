licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "statsd_lib",
    srcs = ["statsd.cc"],
    hdrs = ["statsd.h"],
    external_deps = [
        "envoy_bootstrap",
    ],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/stats:statsd_lib",
        "//source/server:configuration_lib",
    ],
)

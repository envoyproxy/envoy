licenses(["notice"])  # Apache 2
# Stats sink for the DataDog (https://www.datadoghq.com/) variant of the statsd protocol
# (https://docs.datadoghq.com/developers/dogstatsd/).

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/extensions/stat_sinks:well_known_names",
        "//source/extensions/stat_sinks/common/statsd:statsd_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

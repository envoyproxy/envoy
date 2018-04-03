licenses(["notice"])  # Apache 2
# Stats sink for the basic version of the statsd protocol (https://github.com/b/statsd_spec).

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
        "//source/common/config:well_known_names",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/extensions/stat_sinks/common/statsd:statsd_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

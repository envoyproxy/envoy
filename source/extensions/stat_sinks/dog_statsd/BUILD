load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Stats sink for the DataDog (https://www.datadoghq.com/) variant of the statsd protocol
# (https://docs.datadoghq.com/developers/dogstatsd/).

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/extensions/stat_sinks/common/statsd:statsd_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

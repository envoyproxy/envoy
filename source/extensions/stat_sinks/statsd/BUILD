load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Stats sink for the basic version of the statsd protocol (https://github.com/b/statsd_spec).

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

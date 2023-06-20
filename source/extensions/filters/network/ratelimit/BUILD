load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Ratelimit L4 network filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/rate_limit_filter

envoy_extension_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_macros",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/extensions/filters/network/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/ratelimit:ratelimit_lib",
        "@envoy_api//envoy/extensions/filters/network/ratelimit/v3:pkg_cc_proto",
    ],
)

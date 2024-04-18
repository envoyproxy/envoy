load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Connection limit L4 network filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/connection_limit_filter

envoy_extension_package()

envoy_cc_library(
    name = "connection_limit_lib",
    srcs = ["connection_limit.cc"],
    hdrs = ["connection_limit.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_macros",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "@envoy_api//envoy/extensions/filters/network/connection_limit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/connection_limit:connection_limit_lib",
        "@envoy_api//envoy/extensions/filters/network/connection_limit/v3:pkg_cc_proto",
    ],
)

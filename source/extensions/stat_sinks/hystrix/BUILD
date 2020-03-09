licenses(["notice"])  # Apache 2

# Stats sink for the basic version of the hystrix protocol (https://github.com/b/hystrix_spec).

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "data_plane_agnostic",
    deps = [
        "//include/envoy/registry",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/extensions/stat_sinks:well_known_names",
        "//source/extensions/stat_sinks/hystrix:hystrix_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "hystrix_lib",
    srcs = ["hystrix.cc"],
    hdrs = ["hystrix.h"],
    deps = [
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:headers_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

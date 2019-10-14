licenses(["notice"])  # Apache 2

# Trace driver for AWS X-Ray.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "xray_lib",
    srcs = [
        "util.cc",
        "xray_tracer_impl.cc",
    ],
    hdrs = [
        "sampling_strategy.h",
        "tracer.h",
        "util.h",
        "xray_configuration.h",
        "xray_tracer_impl.h",
    ],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":xray_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers:well_known_names",
        "//source/extensions/tracers/common:factory_base_lib",
    ],
)

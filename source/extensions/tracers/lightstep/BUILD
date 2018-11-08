licenses(["notice"])  # Apache 2

# Trace driver for LightStep (https://lightstep.com/)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "lightstep_tracer_lib",
    srcs = [
        "lightstep_tracer_impl.cc",
    ],
    hdrs = [
        "lightstep_tracer_impl.h",
    ],
    external_deps = ["lightstep"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/tracers:well_known_names",
        "//source/extensions/tracers/common/ot:opentracing_driver_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":lightstep_tracer_lib",
        "//source/extensions/tracers:well_known_names",
        "//source/extensions/tracers/common:factory_base_lib",
    ],
)

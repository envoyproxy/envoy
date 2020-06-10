load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Trace driver for dynamically loadable C++ OpenTracing drivers (http://opentracing.io/).

envoy_package()

envoy_cc_library(
    name = "dynamic_opentracing_driver_lib",
    srcs = [
        "dynamic_opentracing_driver_impl.cc",
    ],
    hdrs = [
        "dynamic_opentracing_driver_impl.h",
    ],
    deps = [
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/tracers/common/ot:opentracing_driver_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":dynamic_opentracing_driver_lib",
        "//source/extensions/tracers:well_known_names",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

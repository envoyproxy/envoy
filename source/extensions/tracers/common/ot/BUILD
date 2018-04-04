licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "opentracing_driver_lib",
    srcs = [
        "opentracing_driver_impl.cc",
    ],
    hdrs = [
        "opentracing_driver_impl.h",
    ],
    external_deps = ["opentracing"],
    deps = [
        "//source/common/tracing:http_tracer_lib",
    ],
)

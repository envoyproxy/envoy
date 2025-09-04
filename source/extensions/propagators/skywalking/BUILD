load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "skywalking_propagator_lib",
    hdrs = ["skywalking_propagator.h"],
    deps = [
        "//source/common/singleton:const_singleton",
        "//source/common/tracing:trace_context_lib",
    ],
)

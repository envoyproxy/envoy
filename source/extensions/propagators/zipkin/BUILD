load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "propagator_lib",
    srcs = ["propagator.cc"],
    hdrs = ["propagator.h"],
    deps = [
        "//envoy/tracing:trace_context_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:statusor_lib",
        "//source/extensions/tracers/zipkin:span_context_lib",
    ],
)

envoy_cc_library(
    name = "b3_propagator_lib",
    deps = [
        "//source/extensions/propagators/zipkin/b3:propagator_lib",
    ],
)

envoy_cc_library(
    name = "w3c_trace_context_propagator_lib",
    deps = [
        "//source/extensions/propagators/zipkin/w3c:trace_context_propagator_lib",
    ],
)

envoy_cc_library(
    name = "propagator_factory_lib",
    srcs = ["propagator_factory.cc"],
    hdrs = ["propagator_factory.h"],
    deps = [
        ":b3_propagator_lib",
        ":propagator_lib",
        ":w3c_trace_context_propagator_lib",
        "//envoy/api:api_interface",
        "//source/common/common:logger_lib",
    ],
)

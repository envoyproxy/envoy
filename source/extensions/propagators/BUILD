load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "trace_context_lib",
    srcs = [
        "trace_context.cc",
    ],
    hdrs = [
        "trace_context.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "generic_propagator_interface_lib",
    srcs = [
        "generic_propagator.cc",
    ],
    hdrs = [
        "generic_propagator.h",
    ],
    deps = [
        ":trace_context_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/tracing:trace_context_lib",
    ],
)

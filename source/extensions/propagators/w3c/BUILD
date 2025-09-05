load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "trace_context_lib",
    srcs = ["trace_context.cc"],
    hdrs = ["trace_context.h"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "propagator_lib",
    srcs = ["propagator.cc"],
    hdrs = ["propagator.h"],
    deps = [
        ":trace_context_lib",
        "//envoy/tracing:trace_context_interface",
        "//source/common/singleton:const_singleton",
        "//source/common/tracing:trace_context_lib",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

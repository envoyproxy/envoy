load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "b3_propagator_lib",
    srcs = [
        "propagator.cc",
    ],
    hdrs = [
        "propagator.h",
    ],
    deps = [
        "//source/common/tracing:trace_context_lib",
        "//source/extensions/propagators/opentelemetry:propagator_interface_lib",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

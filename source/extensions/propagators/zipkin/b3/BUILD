load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "propagator_lib",
    srcs = [
        "propagator.cc",
    ],
    hdrs = [
        "propagator.h",
    ],
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/extensions/propagators/zipkin:propagator_lib",
        "//source/extensions/tracers/common/utils:trace_lib",
        "//source/extensions/tracers/zipkin:span_context_lib",
        "@com_google_absl//absl/strings",
    ],
)

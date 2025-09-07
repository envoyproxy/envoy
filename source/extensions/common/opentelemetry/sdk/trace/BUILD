load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "constants_lib",
    hdrs = ["constants.h"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "types_lib",
    hdrs = ["types.h"],
    deps = [
        "@opentelemetry_proto//:trace_proto_cc",
        "@opentelemetry_proto//:trace_service_proto_cc",
    ],
)

envoy_cc_library(
    name = "sdk_trace_lib",
    deps = [
        ":constants_lib",
        ":types_lib",
    ],
)

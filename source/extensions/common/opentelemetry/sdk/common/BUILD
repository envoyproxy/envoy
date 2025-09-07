load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "types_lib",
    hdrs = ["types.h"],
    deps = [
        "@io_opentelemetry_cpp//api",
        "@opentelemetry_proto//:common_proto_cc",
    ],
)

envoy_cc_library(
    name = "sdk_common_lib",
    deps = [
        ":types_lib",
    ],
)

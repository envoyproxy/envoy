load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Trace driver for OpenTelemetry: https://opentelemetry.io/

envoy_extension_package()

envoy_cc_library(
    name = "grpc_trace_exporter",
    srcs = ["grpc_trace_exporter.cc"],
    hdrs = ["grpc_trace_exporter.h"],
    deps = [
        "//envoy/grpc:async_client_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf",
        "@opentelemetry_proto//:trace_cc_proto",
    ],
)

load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "trace_service",
    srcs = ["trace_service.proto"],
    has_services = 1,
    require_py = 0,
    deps = [
        "//envoy/api/v2/core:base",
        "@io_opencensus_trace//:trace_model",
    ],
)

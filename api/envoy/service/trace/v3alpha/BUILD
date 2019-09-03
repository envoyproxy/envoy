load("@envoy_api//bazel:api_build_system.bzl", "api_go_grpc_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "trace_service",
    srcs = ["trace_service.proto"],
    has_services = 1,
    require_py = 0,
    deps = [
        "//envoy/api/v3alpha/core:base",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_proto",
    ],
)

api_go_grpc_library(
    name = "trace_service",
    proto = ":trace_service",
    deps = [
        "//envoy/api/v3alpha/core:base_go_proto",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_go",
    ],
)

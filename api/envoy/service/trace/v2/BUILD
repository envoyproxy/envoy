load("@envoy_api//bazel:api_build_system.bzl", "api_go_grpc_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

# TODO: trace_service grpc stubs require external language-specific dependencies which have only been mapped for Go and
# therefore defines it's own grpc_library definitions eschewing the use of `has_services`.
api_proto_library_internal(
    name = "trace_service",
    srcs = ["trace_service.proto"],
    has_services = 0,
    require_py = 0,
    deps = [
        "//envoy/api/v2/core:base",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_proto",
    ],
)

api_go_grpc_library(
    name = "trace_service",
    proto = ":trace_service",
    deps = [
        "//envoy/api/v2/core:base_go_proto",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_go",
    ],
)

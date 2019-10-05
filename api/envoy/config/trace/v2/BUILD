load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2/core",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto",
    ],
)

api_proto_library_internal(
    name = "trace",
    srcs = ["trace.proto"],
    visibility = [
        "//envoy/config/bootstrap/v2:__pkg__",
    ],
    deps = [
        "//envoy/api/v2/core:grpc_service",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto",
    ],
)

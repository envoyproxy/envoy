load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v3alpha/core",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_proto",
    ],
)

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

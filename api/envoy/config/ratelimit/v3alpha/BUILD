load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/api/v3alpha/core"],
)

api_proto_library_internal(
    name = "rls",
    srcs = ["rls.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha/core:grpc_service",
    ],
)

licenses(["notice"])  # Apache 2

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

api_proto_package(
    deps = ["//envoy/api/v3alpha/core"],
)

api_proto_library_internal(
    name = "file_based_metadata",
    srcs = ["file_based_metadata.proto"],
    deps = ["//envoy/api/v3alpha/core:base"],
)

api_proto_library_internal(
    name = "aws_iam",
    srcs = ["aws_iam.proto"],
)

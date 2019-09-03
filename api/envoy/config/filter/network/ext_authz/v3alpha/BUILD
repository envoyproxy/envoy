load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "ext_authz",
    srcs = ["ext_authz.proto"],
    deps = ["//envoy/api/v3alpha/core:grpc_service"],
)

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/config/rbac/v3alpha:pkg"],
)

api_proto_library_internal(
    name = "rbac",
    srcs = ["rbac.proto"],
    deps = ["//envoy/config/rbac/v3alpha:rbac"],
)

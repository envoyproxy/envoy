load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package()

api_proto_library_internal(
    name = "injected_resource",
    srcs = ["injected_resource.proto"],
    visibility = ["//visibility:public"],
)

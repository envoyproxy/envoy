load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "redis",
    srcs = glob(["*.proto"]),
)

api_proto_library_internal(
    name = "redis_cluster",
    srcs = ["redis_cluster.proto"],
)

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "fixed_heap_protos",
    srcs = glob(["*.proto"]),
)

api_proto_library_internal(
    name = "fixed_heap",
    srcs = ["fixed_heap.proto"],
    visibility = ["//visibility:public"],
)

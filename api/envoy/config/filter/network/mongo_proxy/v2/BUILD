load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "network_mongo_proxy",
    deps = [
        "//envoy/config/filter/fault/v2:fault_config",
    ],
)

api_proto_library_internal(
    name = "mongo_proxy",
    srcs = ["mongo_proxy.proto"],
    deps = ["//envoy/config/filter/fault/v2:fault"],
)

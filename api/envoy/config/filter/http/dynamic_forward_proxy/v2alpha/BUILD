load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "http_dynamic_forward_proxy",
    deps = [
        "//envoy/config/common/dynamic_forward_proxy/v2alpha:dynamic_forward_proxy",
    ],
)

api_proto_library_internal(
    name = "dynamic_forward_proxy",
    srcs = ["dynamic_forward_proxy.proto"],
    deps = [
        "//envoy/config/common/dynamic_forward_proxy/v2alpha:dns_cache",
    ],
)

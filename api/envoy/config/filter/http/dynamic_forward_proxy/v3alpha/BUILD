load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "dynamic_forward_proxy",
    srcs = ["dynamic_forward_proxy.proto"],
    deps = [
        "//envoy/config/common/dynamic_forward_proxy/v3alpha:dns_cache",
    ],
)

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "cluster",
    srcs = ["cluster.proto"],
    deps = [
        "//envoy/config/common/dynamic_forward_proxy/v2alpha:dns_cache",
    ],
)

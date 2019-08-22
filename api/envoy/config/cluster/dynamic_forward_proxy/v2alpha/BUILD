load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "dynamic_forward_proxy_config",
    srcs = glob(["*.proto"]),
    deps = [
        "//envoy/config/common/dynamic_forward_proxy/v2alpha:dynamic_forward_proxy",
    ],
)

api_proto_library_internal(
    name = "cluster",
    srcs = ["cluster.proto"],
    deps = [
        "//envoy/config/common/dynamic_forward_proxy/v2alpha:dns_cache",
    ],
)

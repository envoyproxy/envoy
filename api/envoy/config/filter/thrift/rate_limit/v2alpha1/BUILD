load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2/ratelimit:pkg",
        "//envoy/config/ratelimit/v2:pkg",
    ],
)

api_proto_library_internal(
    name = "rate_limit",
    srcs = ["rate_limit.proto"],
    deps = [
        "//envoy/api/v2/ratelimit",
        "//envoy/config/ratelimit/v2:rls",
    ],
)

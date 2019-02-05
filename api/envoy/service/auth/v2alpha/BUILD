load("//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "external_auth",
    srcs = [
        "external_auth.proto",
    ],
    has_services = 1,
    deps = [
        ":attribute_context",
        "//envoy/api/v2/core:base",
        "//envoy/service/v2/auth:attribute_context",
        "//envoy/type:http_status",
    ],
)

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v2/core",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "attribute_context",
    srcs = [
        "attribute_context.proto",
    ],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
    ],
)

api_proto_library_internal(
    name = "external_auth",
    srcs = [
        "external_auth.proto",
    ],
    has_services = 1,
    visibility = ["//visibility:public"],
    deps = [
        ":attribute_context",
        "//envoy/api/v2/core:base",
        "//envoy/type:http_status",
    ],
)

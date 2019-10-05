licenses(["notice"])  # Apache 2

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/api/v3alpha/route:pkg",
    ],
)

api_proto_library_internal(
    name = "jwt_authn",
    srcs = ["config.proto"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:http_uri",
        "//envoy/api/v3alpha/route",
    ],
)

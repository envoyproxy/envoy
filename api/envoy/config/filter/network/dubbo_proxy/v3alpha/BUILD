load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/api/v3alpha/route:pkg",
        "//envoy/type",
        "//envoy/type/matcher",
    ],
)

api_proto_library_internal(
    name = "dubbo_proxy",
    srcs = [
        "dubbo_proxy.proto",
        "route.proto",
    ],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/route",
        "//envoy/type:range",
        "//envoy/type/matcher:string",
    ],
)

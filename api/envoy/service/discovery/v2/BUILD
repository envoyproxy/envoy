load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v2",
        "//envoy/api/v2/core",
        "//envoy/api/v2/endpoint:pkg",
    ],
)

api_proto_library_internal(
    name = "ads",
    srcs = ["ads.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v2:discovery",
    ],
)

api_proto_library_internal(
    name = "hds",
    srcs = ["hds.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:health_check",
        "//envoy/api/v2/endpoint",
    ],
)

api_proto_library_internal(
    name = "sds",
    srcs = ["sds.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v2:discovery",
    ],
)

api_proto_library_internal(
    name = "rtds",
    srcs = ["rtds.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v2:discovery",
    ],
)

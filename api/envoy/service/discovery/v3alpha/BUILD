load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v3alpha",
        "//envoy/api/v3alpha/core",
        "//envoy/api/v3alpha/endpoint:pkg",
    ],
)

api_proto_library_internal(
    name = "ads",
    srcs = ["ads.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v3alpha:discovery",
    ],
)

api_proto_library_internal(
    name = "hds",
    srcs = ["hds.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:health_check",
        "//envoy/api/v3alpha/endpoint",
    ],
)

api_proto_library_internal(
    name = "sds",
    srcs = ["sds.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v3alpha:discovery",
    ],
)

api_proto_library_internal(
    name = "rtds",
    srcs = ["rtds.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v3alpha:discovery",
    ],
)

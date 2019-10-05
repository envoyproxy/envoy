load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2/auth",
        "//envoy/api/v2/core",
    ],
)

api_proto_library_internal(
    name = "endpoint",
    srcs = ["endpoint.proto"],
    visibility = ["//envoy/api/v2:friends"],
    deps = [
        "//envoy/api/v2/auth:cert",
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:config_source",
        "//envoy/api/v2/core:health_check",
        "//envoy/api/v2/core:protocol",
    ],
)

api_proto_library_internal(
    name = "load_report",
    srcs = ["load_report.proto"],
    visibility = ["//envoy/api/v2:friends"],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
    ],
)

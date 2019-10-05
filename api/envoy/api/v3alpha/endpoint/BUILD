load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/auth",
        "//envoy/api/v3alpha/core",
    ],
)

api_proto_library_internal(
    name = "endpoint",
    srcs = ["endpoint.proto"],
    visibility = ["//envoy/api/v3alpha:friends"],
    deps = [
        "//envoy/api/v3alpha/auth:cert",
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
        "//envoy/api/v3alpha/core:health_check",
        "//envoy/api/v3alpha/core:protocol",
    ],
)

api_proto_library_internal(
    name = "load_report",
    srcs = ["load_report.proto"],
    visibility = ["//envoy/api/v3alpha:friends"],
    deps = [
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
    ],
)

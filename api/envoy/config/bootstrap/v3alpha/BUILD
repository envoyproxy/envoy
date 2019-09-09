load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha",
        "//envoy/api/v3alpha/auth",
        "//envoy/api/v3alpha/core",
        "//envoy/config/metrics/v3alpha:pkg",
        "//envoy/config/overload/v3alpha:pkg",
        "//envoy/config/ratelimit/v3alpha:pkg",
        "//envoy/config/trace/v3alpha:pkg",
    ],
)

api_proto_library_internal(
    name = "bootstrap",
    srcs = ["bootstrap.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha:cds",
        "//envoy/api/v3alpha:lds",
        "//envoy/api/v3alpha/auth:cert",
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
        "//envoy/config/metrics/v3alpha:metrics_service",
        "//envoy/config/metrics/v3alpha:stats",
        "//envoy/config/overload/v3alpha:overload",
        "//envoy/config/ratelimit/v3alpha:rls",
        "//envoy/config/trace/v3alpha:trace",
    ],
)

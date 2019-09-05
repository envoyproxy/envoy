load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2",
        "//envoy/api/v2/auth",
        "//envoy/api/v2/core",
        "//envoy/config/metrics/v2:pkg",
        "//envoy/config/overload/v2alpha:pkg",
        "//envoy/config/ratelimit/v2:pkg",
        "//envoy/config/trace/v2:pkg",
    ],
)

api_proto_library_internal(
    name = "bootstrap",
    srcs = ["bootstrap.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:cds",
        "//envoy/api/v2:lds",
        "//envoy/api/v2/auth:cert",
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:config_source",
        "//envoy/config/metrics/v2:metrics_service",
        "//envoy/config/metrics/v2:stats",
        "//envoy/config/overload/v2alpha:overload",
        "//envoy/config/ratelimit/v2:rls",
        "//envoy/config/trace/v2:trace",
    ],
)

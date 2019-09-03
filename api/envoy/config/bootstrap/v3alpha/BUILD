load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

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

api_go_proto_library(
    name = "bootstrap",
    proto = ":bootstrap",
    deps = [
        "//envoy/api/v3alpha:cds_go_grpc",
        "//envoy/api/v3alpha:lds_go_grpc",
        "//envoy/api/v3alpha/auth:cert_go_proto",
        "//envoy/api/v3alpha/core:address_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/core:config_source_go_proto",
        "//envoy/config/metrics/v3alpha:metrics_service_go_proto",
        "//envoy/config/metrics/v3alpha:stats_go_proto",
        "//envoy/config/overload/v3alpha:overload_go_proto",
        "//envoy/config/ratelimit/v3alpha:rls_go_grpc",
        "//envoy/config/trace/v3alpha:trace_go_proto",
    ],
)

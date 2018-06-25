load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "config_dump",
    srcs = ["config_dump.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:cds",
        "//envoy/api/v2:lds",
        "//envoy/api/v2:rds",
        "//envoy/config/bootstrap/v2:bootstrap",
    ],
)

api_proto_library(
    name = "clusters",
    srcs = ["clusters.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":metrics",
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:health_check",
        "//envoy/type:percent",
    ],
)

api_proto_library(
    name = "metrics",
    srcs = ["metrics.proto"],
    visibility = ["//visibility:public"],
)

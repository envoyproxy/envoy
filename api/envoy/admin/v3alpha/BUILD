load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha",
        "//envoy/api/v3alpha/auth",
        "//envoy/api/v3alpha/core",
        "//envoy/config/bootstrap/v3alpha:pkg",
        "//envoy/service/tap/v3alpha:pkg",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "config_dump",
    srcs = ["config_dump.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha:cds",
        "//envoy/api/v3alpha:lds",
        "//envoy/api/v3alpha:rds",
        "//envoy/api/v3alpha:srds",
        "//envoy/api/v3alpha/auth:cert",
        "//envoy/config/bootstrap/v3alpha:bootstrap",
    ],
)

api_proto_library_internal(
    name = "clusters",
    srcs = ["clusters.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":metrics",
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:health_check",
        "//envoy/type:percent",
    ],
)

api_proto_library_internal(
    name = "listeners",
    srcs = ["listeners.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha/core:address",
    ],
)

api_proto_library_internal(
    name = "metrics",
    srcs = ["metrics.proto"],
    visibility = ["//visibility:public"],
)

api_proto_library_internal(
    name = "memory",
    srcs = ["memory.proto"],
    visibility = ["//visibility:public"],
)

api_proto_library_internal(
    name = "mutex_stats",
    srcs = ["mutex_stats.proto"],
    visibility = ["//visibility:public"],
)

api_proto_library_internal(
    name = "certs",
    srcs = ["certs.proto"],
    visibility = ["//visibility:public"],
)

api_proto_library_internal(
    name = "server_info",
    srcs = ["server_info.proto"],
    visibility = ["//visibility:public"],
)

api_proto_library_internal(
    name = "tap",
    srcs = ["tap.proto"],
    deps = [
        "//envoy/service/tap/v3alpha:common",
    ],
)

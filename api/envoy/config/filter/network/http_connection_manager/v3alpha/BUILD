load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "http_connection_manager",
    srcs = ["http_connection_manager.proto"],
    deps = [
        "//envoy/api/v3alpha:rds",
        "//envoy/api/v3alpha:srds",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
        "//envoy/api/v3alpha/core:protocol",
        "//envoy/config/filter/accesslog/v3alpha:accesslog",
        "//envoy/type:percent",
    ],
)

api_go_proto_library(
    name = "http_connection_manager",
    proto = ":http_connection_manager",
    deps = [
        "//envoy/api/v3alpha:rds_go_grpc",
        "//envoy/api/v3alpha:srds_go_grpc",
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/core:config_source_go_proto",
        "//envoy/api/v3alpha/core:protocol_go_proto",
        "//envoy/config/filter/accesslog/v3alpha:accesslog_go_proto",
        "//envoy/type:percent_go_proto",
    ],
)

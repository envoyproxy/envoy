load("//bazel:api_build_system.bzl", "api_go_grpc_library", "api_go_proto_library", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "metrics_service",
    srcs = ["metrics_service.proto"],
    visibility = [
        "//envoy/config/bootstrap/v2:__pkg__",
    ],
    deps = [
        "//envoy/api/v2/core:grpc_service",
    ],
)

api_go_proto_library(
    name = "metrics_service",
    proto = ":metrics_service",
    deps = [
        "//envoy/api/v2/core:grpc_service_go_proto",
    ],
)

api_proto_library(
    name = "stats",
    srcs = ["stats.proto"],
    visibility = [
        "//envoy/config/bootstrap/v2:__pkg__",
    ],
    deps = [
        "//envoy/api/v2/core:address",
    ],
)

api_go_proto_library(
    name = "stats",
    proto = ":stats",
    deps = [
        "//envoy/api/v2/core:address_go_proto",
    ],
)

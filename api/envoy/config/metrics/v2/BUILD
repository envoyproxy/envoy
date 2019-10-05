load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2/core",
        "//envoy/type/matcher",
    ],
)

api_proto_library_internal(
    name = "metrics_service",
    srcs = ["metrics_service.proto"],
    visibility = [
        "//envoy/config/bootstrap/v2:__pkg__",
    ],
    deps = [
        "//envoy/api/v2/core:grpc_service",
    ],
)

api_proto_library_internal(
    name = "stats",
    srcs = ["stats.proto"],
    visibility = [
        "//envoy/config/bootstrap/v2:__pkg__",
    ],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/type/matcher:string",
    ],
)

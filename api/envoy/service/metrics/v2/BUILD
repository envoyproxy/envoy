load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v2/core",
        "@prometheus_metrics_model//:client_model",
    ],
)

api_proto_library_internal(
    name = "metrics_service",
    srcs = ["metrics_service.proto"],
    has_services = 0,
    require_py = 0,
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:grpc_service",
        "@prometheus_metrics_model//:client_model",
    ],
)

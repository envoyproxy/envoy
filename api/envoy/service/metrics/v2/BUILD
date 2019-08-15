load("@envoy_api//bazel:api_build_system.bzl", "api_go_grpc_library", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

# TODO: metrics_service grpc stubs require external language-specific dependencies which have only been mapped for Go and
# therefore defines it's own grpc_library definitions eschewing the use of `has_services`.
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

api_go_grpc_library(
    name = "metrics_service",
    proto = ":metrics_service",
    deps = [
        "//envoy/api/v2/core:base_go_proto",
        "@prometheus_metrics_model//:client_model_go_proto",
    ],
)

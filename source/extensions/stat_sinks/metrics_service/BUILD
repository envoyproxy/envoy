licenses(["notice"])  # Apache 2

# Stats sink for the gRPC metrics service: api/envoy/service/metrics/v2/metrics_service.proto

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "metrics_service_grpc_lib",
    srcs = ["grpc_metrics_service_impl.cc"],
    hdrs = ["grpc_metrics_service_impl.h"],
    deps = [
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/service/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "metrics_proto_descriptors_lib",
    srcs = ["grpc_metrics_proto_descriptors.cc"],
    hdrs = ["grpc_metrics_proto_descriptors.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/config:api_version_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/metrics/v2:pkg_cc_proto",
        "@envoy_api//envoy/service/metrics/v2:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "data_plane_agnostic",
    deps = [
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/stat_sinks:well_known_names",
        "//source/extensions/stat_sinks/metrics_service:metrics_proto_descriptors_lib",
        "//source/extensions/stat_sinks/metrics_service:metrics_service_grpc_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "async_client_interface",
    hdrs = ["async_client.h"],
    deps = [
        ":status",
        "//envoy/buffer:buffer_interface",
        "//envoy/http:async_client_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/tracing:tracer_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "async_client_manager_interface",
    hdrs = ["async_client_manager.h"],
    deps = [
        ":async_client_interface",
        "//envoy/stats:stats_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
    deps = [
        "//envoy/http:context_interface",
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "google_grpc_creds_interface",
    hdrs = ["google_grpc_creds.h"],
    external_deps = ["grpc"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/config:typed_config_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "status",
    hdrs = ["status.h"],
)

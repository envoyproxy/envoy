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
    external_deps = ["abseil_optional"],
    deps = [
        ":status",
        "//include/envoy/buffer:buffer_interface_no_include_prefix",
        "//include/envoy/http:async_client_interface_no_include_prefix",
        "//include/envoy/http:header_map_interface_no_include_prefix",
        "//include/envoy/tracing:http_tracer_interface_no_include_prefix",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "async_client_manager_interface",
    hdrs = ["async_client_manager.h"],
    deps = [
        ":async_client_interface",
        "//include/envoy/stats:stats_interface_no_include_prefix",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
    deps = [
        "//include/envoy/http:context_interface_no_include_prefix",
        "//include/envoy/upstream:upstream_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "google_grpc_creds_interface",
    hdrs = ["google_grpc_creds.h"],
    external_deps = [
        "grpc",
    ],
    deps = [
        "//include/envoy/api:api_interface_no_include_prefix",
        "//include/envoy/config:typed_config_interface_no_include_prefix",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "status",
    hdrs = ["status.h"],
)

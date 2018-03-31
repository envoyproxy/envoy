licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "health_checker_base_lib",
    srcs = ["health_checker_base_impl.cc"],
    hdrs = ["health_checker_base_impl.h"],
    deps = [
        "//include/envoy/upstream:health_checker_interface",
        "//source/common/router:router_lib",
        "@envoy_api//envoy/api/v2/core:health_check_cc",
    ],
)

envoy_cc_library(
    name = "health_checker_lib",
    srcs = ["health_checker_impl.cc"],
    hdrs = ["health_checker_impl.h"],
    external_deps = [
        "grpc_health_proto",
    ],
    deps = [
        ":health_checker_base_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/grpc:status",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:health_checker_config_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:hex_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:host_utility_lib",
        # TODO(mattklein123): The following should not be required once we allow pluggable health
        # checkers.
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
    ],
)

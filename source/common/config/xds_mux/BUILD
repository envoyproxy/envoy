load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "delta_subscription_state_lib",
    srcs = ["delta_subscription_state.cc"],
    hdrs = ["delta_subscription_state.h"],
    deps = [
        ":subscription_state_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "sotw_subscription_state_lib",
    srcs = ["sotw_subscription_state.cc"],
    hdrs = ["sotw_subscription_state.h"],
    deps = [
        ":subscription_state_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:xds_source_id_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "subscription_state_lib",
    hdrs = ["subscription_state.h"],
    deps = [
        "//envoy/config:subscription_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/local_info:local_info_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:ttl_lib",
        "//source/common/config:update_ack_lib",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_mux_lib",
    srcs = ["grpc_mux_impl.cc"],
    hdrs = ["grpc_mux_impl.h"],
    deps = [
        ":delta_subscription_state_lib",
        ":sotw_subscription_state_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
        "//source/common/config:grpc_stream_lib",
        "//source/common/config:pausable_ack_queue_lib",
        "//source/common/config:watch_map_lib",
        "//source/common/config:xds_context_params_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/memory:utils_lib",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

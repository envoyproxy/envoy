load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

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
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "//source/extensions/config_subscription/grpc:xds_source_id_lib",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "subscription_state_lib",
    hdrs = ["subscription_state.h"],
    deps = [
        "//envoy/config:subscription_interface",
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/local_info:local_info_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:ttl_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/config_subscription/grpc:update_ack_lib",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "grpc_mux_lib",
    srcs = ["grpc_mux_impl.cc"],
    hdrs = ["grpc_mux_impl.h"],
    deps = [
        ":delta_subscription_state_lib",
        ":sotw_subscription_state_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
        "//source/common/config:xds_context_params_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/memory:utils_lib",
        "//source/extensions/config_subscription/grpc:eds_resources_cache_lib",
        "//source/extensions/config_subscription/grpc:grpc_mux_context_lib",
        "//source/extensions/config_subscription/grpc:grpc_mux_failover_lib",
        "//source/extensions/config_subscription/grpc:grpc_stream_lib",
        "//source/extensions/config_subscription/grpc:pausable_ack_queue_lib",
        "//source/extensions/config_subscription/grpc:watch_map_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

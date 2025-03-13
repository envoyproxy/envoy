load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "grpc_mux_context_lib",
    hdrs = ["grpc_mux_context.h"],
    deps = [
        "//envoy/config:custom_config_validators_interface",
        "//envoy/config:eds_resources_cache_interface",
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_extension(
    name = "grpc_mux_lib",
    srcs = ["grpc_mux_impl.cc"],
    hdrs = ["grpc_mux_impl.h"],
    deps = [
        ":eds_resources_cache_lib",
        ":grpc_mux_context_lib",
        ":grpc_mux_failover_lib",
        ":grpc_stream_lib",
        ":xds_source_id_lib",
        "//envoy/config:custom_config_validators_interface",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_interface",
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:ttl_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:xds_context_params_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/memory:utils_lib",
        "//source/common/protobuf",
        "@com_google_absl//absl/container:btree",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "new_grpc_mux_lib",
    srcs = ["new_grpc_mux_impl.cc"],
    hdrs = ["new_grpc_mux_impl.h"],
    deps = [
        ":delta_subscription_state_lib",
        ":eds_resources_cache_lib",
        ":grpc_mux_context_lib",
        ":grpc_mux_failover_lib",
        ":grpc_stream_lib",
        ":pausable_ack_queue_lib",
        ":watch_map_lib",
        "//envoy/config:custom_config_validators_interface",
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
        "//source/common/config:xds_context_params_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/memory:utils_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_subscription_impl_lib",
    srcs = ["grpc_subscription_impl.cc"],
    hdrs = ["grpc_subscription_impl.h"],
    deps = [
        ":eds_resources_cache_lib",
        ":grpc_mux_lib",
        ":new_grpc_mux_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
        "//source/common/config:custom_config_validators_lib",
        "//source/common/config:type_to_endpoint_lib",
        "//source/common/config:xds_resource_lib",
        "//source/extensions/config_subscription/grpc/xds_mux:grpc_mux_lib",
    ],
)

envoy_cc_extension(
    name = "grpc_collection_subscription_lib",
    srcs = ["grpc_collection_subscription_factory.cc"],
    hdrs = ["grpc_collection_subscription_factory.h"],
    extra_visibility = [
        # previously considered core code.
        "//test:__subpackages__",
    ],
    deps = [
        ":grpc_subscription_impl_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:custom_config_validators_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:type_to_endpoint_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:watched_directory_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_extension(
    name = "grpc_subscription_lib",
    srcs = ["grpc_subscription_factory.cc"],
    hdrs = ["grpc_subscription_factory.h"],
    extra_visibility = [
        # previously considered core code.
        "//test:__subpackages__",
    ],
    deps = [
        ":grpc_subscription_impl_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:custom_config_validators_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:type_to_endpoint_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:watched_directory_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "delta_subscription_state_lib",
    srcs = [
        "delta_subscription_state.cc",
    ],
    hdrs = [
        "delta_subscription_state.h",
    ],
    deps = [
        ":pausable_ack_queue_lib",
        ":watch_map_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:ttl_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_stream_interface",
    hdrs = ["grpc_stream_interface.h"],
    deps = [
        "//source/common/grpc:async_client_lib",
    ],
)

envoy_cc_library(
    name = "grpc_stream_lib",
    hdrs = ["grpc_stream.h"],
    deps = [
        ":grpc_stream_interface",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "grpc_mux_failover_lib",
    hdrs = ["grpc_mux_failover.h"],
    deps = [
        ":grpc_stream_lib",
    ],
)

envoy_cc_library(
    name = "pausable_ack_queue_lib",
    srcs = ["pausable_ack_queue.cc"],
    hdrs = ["pausable_ack_queue.h"],
    deps = [
        ":update_ack_lib",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "xds_source_id_lib",
    srcs = ["xds_source_id.cc"],
    hdrs = ["xds_source_id.h"],
    deps = [
        "//envoy/config:xds_resources_delegate_interface",
    ],
)

envoy_cc_library(
    name = "update_ack_lib",
    hdrs = ["update_ack.h"],
    deps = ["@com_google_googleapis//google/rpc:status_cc_proto"],
)

envoy_cc_library(
    name = "watch_map_lib",
    srcs = ["watch_map.cc"],
    hdrs = ["watch_map.h"],
    deps = [
        "//envoy/config:custom_config_validators_interface",
        "//envoy/config:subscription_interface",
        "//envoy/config:xds_config_tracker_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:resource_name_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "eds_resources_cache_lib",
    srcs = ["eds_resources_cache_impl.cc"],
    hdrs = ["eds_resources_cache_impl.h"],
    deps = [
        "//envoy/config:eds_resources_cache_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

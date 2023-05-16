load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "grpc_mux_lib",
    srcs = ["grpc_mux_impl.cc"],
    hdrs = ["grpc_mux_impl.h"],
    deps = [
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
        "//source/common/config:grpc_stream_lib",
        "//source/common/config:ttl_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:xds_context_params_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/config:xds_source_id_lib",
        "//source/common/memory:utils_lib",
        "//source/common/protobuf",
        "@com_google_absl//absl/container:btree",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "new_grpc_mux_lib",
    srcs = ["new_grpc_mux_impl.cc"],
    hdrs = ["new_grpc_mux_impl.h"],
    deps = [
        ":delta_subscription_state_lib",
        "//envoy/config:custom_config_validators_interface",
        "//envoy/config:xds_config_tracker_interface",
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

envoy_cc_library(
    name = "grpc_subscription_impl_lib",
    srcs = ["grpc_subscription_impl.cc"],
    hdrs = ["grpc_subscription_impl.h"],
    deps = [
        ":grpc_mux_lib",
        ":new_grpc_mux_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
        "//source/common/config:custom_config_validators_lib",
        "//source/common/config:type_to_endpoint_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/config/xds_mux:grpc_mux_lib",
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
        "new_delta_subscription_state.cc",
        "old_delta_subscription_state.cc",
    ],
    hdrs = [
        "delta_subscription_state.h",
        "new_delta_subscription_state.h",
        "old_delta_subscription_state.h",
    ],
    deps = [
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:pausable_ack_queue_lib",
        "//source/common/config:ttl_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:watch_map_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

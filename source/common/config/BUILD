load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "api_version_lib",
    hdrs = ["api_version.h"],
)

envoy_cc_library(
    name = "config_provider_lib",
    srcs = ["config_provider_impl.cc"],
    hdrs = ["config_provider_impl.h"],
    deps = [
        ":utility_lib",
        "//envoy/config:config_provider_interface",
        "//envoy/config:config_provider_manager_interface",
        "//envoy/init:manager_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:config_tracker_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/init:watcher_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "context_provider_lib",
    hdrs = ["context_provider_impl.h"],
    deps = [
        ":xds_context_params_lib",
        "//envoy/config:context_provider_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "datasource_lib",
    srcs = ["datasource.cc"],
    hdrs = ["datasource.h"],
    deps = [
        ":remote_data_fetcher_lib",
        "//envoy/api:api_interface",
        "//envoy/init:manager_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:backoff_lib",
        "//source/common/common:empty_string",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "decoded_resource_lib",
    hdrs = ["decoded_resource_impl.h"],
    deps = [
        "//envoy/config:subscription_interface",
        "//source/common/protobuf:utility_lib",
        "@com_github_cncf_udpa//xds/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ttl_lib",
    srcs = ["ttl.cc"],
    hdrs = ["ttl.h"],
    deps = [
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "@com_google_absl//absl/container:btree",
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
        ":api_version_lib",
        ":pausable_ack_queue_lib",
        ":ttl_lib",
        ":utility_lib",
        ":watch_map_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filesystem_subscription_lib",
    srcs = ["filesystem_subscription_impl.cc"],
    hdrs = ["filesystem_subscription_impl.h"],
    deps = [
        ":decoded_resource_lib",
        ":watched_directory_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/filesystem:filesystem_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_stream_lib",
    hdrs = ["grpc_stream.h"],
    deps = [
        ":utility_lib",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "grpc_mux_lib",
    srcs = ["grpc_mux_impl.cc"],
    hdrs = ["grpc_mux_impl.h"],
    deps = [
        ":api_version_lib",
        ":custom_config_validators_interface",
        ":decoded_resource_lib",
        ":grpc_stream_lib",
        ":ttl_lib",
        ":utility_lib",
        ":xds_source_id_lib",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/memory:utils_lib",
        "//source/common/protobuf",
        "@com_google_absl//absl/container:btree",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_subscription_lib",
    srcs = ["grpc_subscription_impl.cc"],
    hdrs = ["grpc_subscription_impl.h"],
    deps = [
        ":grpc_mux_lib",
        ":xds_resource_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
    ],
)

envoy_cc_library(
    name = "new_grpc_mux_lib",
    srcs = ["new_grpc_mux_impl.cc"],
    hdrs = ["new_grpc_mux_impl.h"],
    deps = [
        ":custom_config_validators_interface",
        ":delta_subscription_state_lib",
        ":grpc_stream_lib",
        ":pausable_ack_queue_lib",
        ":watch_map_lib",
        ":xds_context_params_lib",
        ":xds_resource_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_interface",
        "//source/common/memory:utils_lib",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "http_subscription_lib",
    srcs = ["http_subscription_impl.cc"],
    hdrs = ["http_subscription_impl.h"],
    external_deps = [
        "http_api_protos",
    ],
    deps = [
        ":api_version_lib",
        ":decoded_resource_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    srcs = ["metadata.cc"],
    hdrs = ["metadata.h"],
    deps = [
        "//envoy/config:typed_metadata_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/registry",
        "//envoy/singleton:manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/shared_pool:shared_pool_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "opaque_resource_decoder_lib",
    hdrs = ["opaque_resource_decoder_impl.h"],
    deps = [
        "//envoy/config:subscription_interface",
        "//source/common/protobuf:utility_lib",
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
    name = "protobuf_link_hacks",
    hdrs = ["protobuf_link_hacks.h"],
    deps = [
        "@envoy_api//envoy/service/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/extension/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/health/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "remote_data_fetcher_lib",
    srcs = ["remote_data_fetcher.cc"],
    hdrs = ["remote_data_fetcher.h"],
    deps = [
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resource_name_lib",
    hdrs = ["resource_name.h"],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_library(
    name = "runtime_utility_lib",
    srcs = ["runtime_utility.cc"],
    hdrs = ["runtime_utility.h"],
    deps = ["@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto"],
)

envoy_cc_library(
    name = "subscription_factory_lib",
    srcs = ["subscription_factory_impl.cc"],
    hdrs = ["subscription_factory_impl.h"],
    deps = [
        ":custom_config_validators_lib",
        ":filesystem_subscription_lib",
        ":grpc_subscription_lib",
        ":http_subscription_lib",
        ":new_grpc_mux_lib",
        ":type_to_endpoint_lib",
        ":utility_lib",
        ":xds_resource_lib",
        "//envoy/config:subscription_factory_interface",
        "//envoy/config:subscription_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/server:instance_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config/xds_mux:grpc_mux_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "type_to_endpoint_lib",
    srcs = ["type_to_endpoint.cc"],
    hdrs = ["type_to_endpoint.h"],
    deps = [
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/annotations:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "xds_context_params_lib",
    srcs = ["xds_context_params.cc"],
    hdrs = ["xds_context_params.h"],
    deps = [
        "//source/common/common:macros",
        "//source/common/protobuf:utility_lib",
        "@com_github_cncf_udpa//xds/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "xds_resource_lib",
    srcs = ["xds_resource.cc"],
    hdrs = ["xds_resource.h"],
    deps = [
        "//source/common/http:utility_lib",
        "@com_github_cncf_udpa//xds/core/v3:pkg_cc_proto",
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
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:hex_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/stats:histogram_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stats:stats_matcher_lib",
        "//source/common/stats:tag_producer_lib",
        "//source/common/version:api_version_lib",
        "@com_github_cncf_udpa//udpa/type/v1:pkg_cc_proto",
        "@com_github_cncf_udpa//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "watch_map_lib",
    srcs = ["watch_map.cc"],
    hdrs = ["watch_map.h"],
    deps = [
        ":custom_config_validators_interface",
        ":decoded_resource_lib",
        ":utility_lib",
        ":xds_resource_lib",
        "//envoy/config:subscription_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "subscription_base_interface",
    hdrs = ["subscription_base.h"],
    deps = [
        ":opaque_resource_decoder_lib",
        ":resource_name_lib",
        "//envoy/config:subscription_interface",
    ],
)

envoy_cc_library(
    name = "custom_config_validators_interface",
    hdrs = ["custom_config_validators.h"],
    deps = [
        "//envoy/config:config_validator_interface",
    ],
)

envoy_cc_library(
    name = "custom_config_validators_lib",
    srcs = ["custom_config_validators_impl.cc"],
    hdrs = ["custom_config_validators_impl.h"],
    deps = [
        ":custom_config_validators_interface",
        ":opaque_resource_decoder_lib",
        ":resource_name_lib",
        "//envoy/server:instance_interface",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "watched_directory_lib",
    srcs = ["watched_directory.cc"],
    hdrs = ["watched_directory.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/filesystem:watcher_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    srcs = ["well_known_names.cc"],
    hdrs = ["well_known_names.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:regex_lib",
        "//source/common/singleton:const_singleton",
    ],
)

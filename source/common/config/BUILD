licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "address_json_lib",
    srcs = ["address_json.cc"],
    hdrs = ["address_json.h"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "base_json_lib",
    srcs = ["base_json.cc"],
    hdrs = ["base_json.h"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_provider_lib",
    srcs = ["config_provider_impl.cc"],
    hdrs = ["config_provider_impl.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/config:config_provider_interface",
        "//include/envoy/config:config_provider_manager_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:config_tracker_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/init:target_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "datasource_lib",
    srcs = ["datasource.cc"],
    hdrs = ["datasource.h"],
    deps = [
        ":remote_data_fetcher_lib",
        "//include/envoy/api:api_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "delta_subscription_lib",
    srcs = ["delta_subscription_impl.cc"],
    hdrs = ["delta_subscription_impl.h"],
    deps = [
        ":grpc_stream_lib",
        ":new_grpc_mux_lib",
        ":utility_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "delta_subscription_state_lib",
    srcs = ["delta_subscription_state.cc"],
    hdrs = ["delta_subscription_state.h"],
    deps = [
        ":pausable_ack_queue_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filesystem_subscription_lib",
    srcs = ["filesystem_subscription_impl.cc"],
    hdrs = ["filesystem_subscription_impl.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "grpc_stream_lib",
    hdrs = ["grpc_stream.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "filter_json_lib",
    srcs = ["filter_json.cc"],
    hdrs = ["filter_json.h"],
    deps = [
        ":address_json_lib",
        ":json_utility_lib",
        ":protocol_json_lib",
        ":rds_json_lib",
        ":utility_lib",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/access_loggers:well_known_names",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/config/accesslog/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/buffer/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/fault/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/gzip/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/health_check/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/lua/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/rate_limit/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/router/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/squash/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/transcoder/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/client_ssl_auth/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/mongo_proxy/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/tcp_proxy/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_mux_lib",
    srcs = ["grpc_mux_impl.cc"],
    hdrs = ["grpc_mux_impl.h"],
    deps = [
        ":grpc_stream_lib",
        ":utility_lib",
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "grpc_mux_subscription_lib",
    srcs = ["grpc_mux_subscription_impl.cc"],
    hdrs = ["grpc_mux_subscription_impl.h"],
    deps = [
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_subscription_lib",
    hdrs = ["grpc_subscription_impl.h"],
    deps = [
        ":grpc_mux_lib",
        ":grpc_mux_subscription_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/grpc:async_client_interface",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "new_grpc_mux_lib",
    srcs = ["new_grpc_mux_impl.cc"],
    hdrs = ["new_grpc_mux_impl.h"],
    deps = [
        ":delta_subscription_state_lib",
        ":grpc_stream_lib",
        ":pausable_ack_queue_lib",
        ":watch_map_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/grpc:async_client_interface",
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
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "json_utility_lib",
    hdrs = ["json_utility.h"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    srcs = ["metadata.cc"],
    hdrs = ["metadata.h"],
    deps = [
        "//include/envoy/config:typed_metadata_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "pausable_ack_queue_lib",
    srcs = ["pausable_ack_queue.cc"],
    hdrs = ["pausable_ack_queue.h"],
    deps = [
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "protobuf_link_hacks",
    hdrs = ["protobuf_link_hacks.h"],
    deps = [
        "@envoy_api//envoy/service/discovery/v2:pkg_cc_proto",
        "@envoy_api//envoy/service/ratelimit/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "protocol_json_lib",
    srcs = ["protocol_json.cc"],
    hdrs = ["protocol_json.h"],
    deps = [
        ":json_utility_lib",
        "//include/envoy/json:json_object_interface",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rds_json_lib",
    srcs = ["rds_json.cc"],
    hdrs = ["rds_json.h"],
    deps = [
        ":base_json_lib",
        ":json_utility_lib",
        ":metadata_lib",
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/json:config_schemas_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "remote_data_fetcher_lib",
    srcs = ["remote_data_fetcher.cc"],
    hdrs = ["remote_data_fetcher.h"],
    deps = [
        "//bazel:crypto_utility_lib",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resources_lib",
    hdrs = ["resources.h"],
    deps = ["//source/common/singleton:const_singleton"],
)

envoy_cc_library(
    name = "runtime_utility_lib",
    srcs = ["runtime_utility.cc"],
    hdrs = ["runtime_utility.h"],
    deps = ["@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto"],
)

envoy_cc_library(
    name = "subscription_factory_lib",
    srcs = ["subscription_factory_impl.cc"],
    hdrs = ["subscription_factory_impl.h"],
    deps = [
        ":delta_subscription_lib",
        ":filesystem_subscription_lib",
        ":grpc_mux_subscription_lib",
        ":grpc_subscription_lib",
        ":http_subscription_lib",
        ":type_to_endpoint_lib",
        ":utility_lib",
        "//include/envoy/config:subscription_factory_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "type_to_endpoint_lib",
    srcs = ["type_to_endpoint.cc"],
    hdrs = ["type_to_endpoint.h"],
    deps = [
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":json_utility_lib",
        ":resources_lib",
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:hex_lib",
        "//source/common/grpc:common_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/stats:stats_lib",
        "//source/common/stats:stats_matcher_lib",
        "//source/common/stats:tag_producer_lib",
        "@com_github_cncf_udpa//udpa/type/v1:typed_struct_cc",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "version_converter_lib",
    srcs = ["version_converter.cc"],
    hdrs = ["version_converter.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "watch_map_lib",
    srcs = ["watch_map.cc"],
    hdrs = ["watch_map.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    srcs = ["well_known_names.cc"],
    hdrs = ["well_known_names.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/singleton:const_singleton",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "alternate_protocols_cache_interface",
    hdrs = ["alternate_protocols_cache.h"],
    deps = [
        "//include/envoy/common:time_interface_no_include_prefix",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "api_listener_interface",
    hdrs = ["api_listener.h"],
    deps = [":codec_interface_no_include_prefix"],
)

envoy_cc_library(
    name = "async_client_interface",
    hdrs = ["async_client.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":message_interface_no_include_prefix",
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "//include/envoy/tracing:http_tracer_interface_no_include_prefix",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = [
        ":header_map_interface_no_include_prefix",
        ":metadata_interface_no_include_prefix",
        ":protocol_interface_no_include_prefix",
        "//include/envoy/buffer:buffer_interface_no_include_prefix",
        "//include/envoy/grpc:status_no_include_prefix",
        "//include/envoy/network:address_interface_no_include_prefix",
        "//include/envoy/stream_info:stream_info_interface_no_include_prefix",
        "//source/common/http:status_lib",
    ],
)

envoy_cc_library(
    name = "codes_interface",
    hdrs = ["codes.h"],
    deps = ["//include/envoy/stats:stats_interface_no_include_prefix"],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        ":codec_interface_no_include_prefix",
        "//include/envoy/common:conn_pool_interface_no_include_prefix",
        "//include/envoy/event:deferred_deletable_no_include_prefix",
        "//include/envoy/upstream:upstream_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
    deps = [
        ":codes_interface_no_include_prefix",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_interface",
    hdrs = ["filter.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":codec_interface_no_include_prefix",
        ":header_map_interface_no_include_prefix",
        "//include/envoy/access_log:access_log_interface_no_include_prefix",
        "//include/envoy/common:scope_tracker_interface_no_include_prefix",
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "//include/envoy/grpc:status_no_include_prefix",
        "//include/envoy/matcher:matcher_interface_no_include_prefix",
        "//include/envoy/router:router_interface_no_include_prefix",
        "//include/envoy/ssl:connection_interface_no_include_prefix",
        "//include/envoy/stream_info:stream_info_interface_no_include_prefix",
        "//include/envoy/tracing:http_tracer_interface_no_include_prefix",
        "//source/common/common:scope_tracked_object_stack",
    ],
)

envoy_cc_library(
    name = "hash_policy_interface",
    hdrs = ["hash_policy.h"],
    deps = [
        ":header_map_interface_no_include_prefix",
        "//include/envoy/network:address_interface_no_include_prefix",
        "//include/envoy/stream_info:filter_state_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "header_map_interface",
    hdrs = ["header_map.h"],
    external_deps = [
        "abseil_inlined_vector",
    ],
    deps = [
        ":header_formatter_interface_no_include_prefix",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
    ],
)

envoy_cc_library(
    name = "message_interface",
    hdrs = ["message.h"],
    deps = [
        ":header_map_interface_no_include_prefix",
        "//include/envoy/buffer:buffer_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = ["protocol.h"],
)

envoy_cc_library(
    name = "query_params_interface",
    hdrs = ["query_params.h"],
)

envoy_cc_library(
    name = "metadata_interface",
    hdrs = ["metadata_interface.h"],
    external_deps = ["abseil_node_hash_map"],
)

envoy_cc_library(
    name = "request_id_extension_interface",
    hdrs = ["request_id_extension.h"],
    deps = [
        ":header_map_interface_no_include_prefix",
        "//include/envoy/tracing:trace_reason_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "header_formatter_interface",
    hdrs = ["header_formatter.h"],
    deps = [
        "//include/envoy/config:typed_config_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "original_ip_detection_interface",
    hdrs = ["original_ip_detection.h"],
    deps = [
        ":codes_interface_no_include_prefix",
        ":header_map_interface_no_include_prefix",
        "//include/envoy/config:typed_config_interface_no_include_prefix",
        "//include/envoy/network:address_interface_no_include_prefix",
        "//include/envoy/server:factory_context_interface_no_include_prefix",
    ],
)

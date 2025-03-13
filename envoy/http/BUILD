load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "http_server_properties_cache_interface",
    hdrs = ["http_server_properties_cache.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/event:dispatcher_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "api_listener_interface",
    hdrs = ["api_listener.h"],
    deps = [":codec_interface"],
)

envoy_cc_library(
    name = "async_client_interface",
    hdrs = ["async_client.h"],
    deps = [
        ":filter_interface",
        ":message_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/tracing:tracer_interface",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = [
        ":header_map_interface",
        ":metadata_interface",
        ":protocol_interface",
        ":stream_reset_handler_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/grpc:status",
        "//envoy/network:address_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/http:status_lib",
    ],
)

envoy_cc_library(
    name = "stream_reset_handler_interface",
    hdrs = ["stream_reset_handler.h"],
)

envoy_cc_library(
    name = "codes_interface",
    hdrs = ["codes.h"],
    deps = ["//envoy/stats:stats_interface"],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        ":codec_interface",
        "//envoy/common:conn_pool_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
    deps = [
        ":codes_interface",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_factory_interface",
    hdrs = ["filter_factory.h"],
    deps = [
        ":header_map_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/grpc:status",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "filter_interface",
    hdrs = ["filter.h"],
    deps = [
        ":codec_interface",
        ":filter_factory_interface",
        ":header_map_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/common:scope_tracker_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:status",
        "//envoy/matcher:matcher_interface",
        "//envoy/router:router_interface",
        "//envoy/router:scopes_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/tracing:tracer_interface",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/common:scope_tracked_object_stack",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "hash_policy_interface",
    hdrs = ["hash_policy.h"],
    deps = [
        ":header_map_interface",
        "//envoy/network:address_interface",
        "//envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_library(
    name = "header_map_interface",
    hdrs = ["header_map.h"],
    deps = [
        ":header_formatter_interface",
        "//envoy/common:union_string",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "@com_google_absl//absl/container:inlined_vector",
    ],
)

envoy_cc_library(
    name = "message_interface",
    hdrs = ["message.h"],
    deps = [
        ":header_map_interface",
        "//envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = ["protocol.h"],
)

envoy_cc_library(
    name = "query_params_interface",
    hdrs = ["query_params.h"],
    deps = ["@com_google_absl//absl/container:btree"],
)

envoy_cc_library(
    name = "metadata_interface",
    hdrs = ["metadata_interface.h"],
    deps = ["@com_google_absl//absl/container:node_hash_map"],
)

envoy_cc_library(
    name = "request_id_extension_interface",
    hdrs = ["request_id_extension.h"],
    deps = [
        ":header_map_interface",
        "//envoy/tracing:trace_reason_interface",
    ],
)

envoy_cc_library(
    name = "header_formatter_interface",
    hdrs = ["header_formatter.h"],
    deps = [
        "//envoy/common:optref_lib",
        "//envoy/config:typed_config_interface",
    ],
)

envoy_cc_library(
    name = "original_ip_detection_interface",
    hdrs = ["original_ip_detection.h"],
    deps = [
        ":codes_interface",
        ":header_map_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/network:address_interface",
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "early_header_mutation_interface",
    hdrs = ["early_header_mutation.h"],
    deps = [
        ":header_map_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "header_evaluator",
    hdrs = ["header_evaluator.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "stateful_session_interface",
    hdrs = ["stateful_session.h"],
    deps = [
        ":header_map_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "persistent_quic_info_interface",
    hdrs = ["persistent_quic_info.h"],
)

envoy_cc_library(
    name = "header_validator_interface",
    hdrs = ["header_validator.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//envoy/http:protocol_interface",
    ],
)

envoy_cc_library(
    name = "header_validator_factory_interface",
    hdrs = ["header_validator_factory.h"],
    deps = [
        ":header_validator_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "header_validator_errors",
    hdrs = [
        "header_validator_errors.h",
    ],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)

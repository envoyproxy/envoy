licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "api_listener_interface",
    hdrs = ["api_listener.h"],
    deps = [":codec_interface"],
)

envoy_cc_library(
    name = "async_client_interface",
    hdrs = ["async_client.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":message_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/protobuf",
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
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:address_interface",
    ],
)

envoy_cc_library(
    name = "codes_interface",
    hdrs = ["codes.h"],
    deps = ["//include/envoy/stats:stats_interface"],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        ":codec_interface",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/upstream:upstream_interface",
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
    name = "filter_interface",
    hdrs = ["filter.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":codec_interface",
        ":header_map_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/common:scope_tracker_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/grpc:status",
        "//include/envoy/router:router_interface",
        "//include/envoy/ssl:connection_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//include/envoy/tracing:http_tracer_interface",
    ],
)

envoy_cc_library(
    name = "hash_policy_interface",
    hdrs = ["hash_policy.h"],
    deps = [
        ":header_map_interface",
        "//include/envoy/network:address_interface",
    ],
)

envoy_cc_library(
    name = "header_map_interface",
    hdrs = ["header_map.h"],
    external_deps = [
        "abseil_inlined_vector",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
    ],
)

envoy_cc_library(
    name = "message_interface",
    hdrs = ["message.h"],
    deps = [
        ":header_map_interface",
        "//include/envoy/buffer:buffer_interface",
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
)

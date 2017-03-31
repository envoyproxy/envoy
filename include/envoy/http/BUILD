package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "access_log_includes",
    hdrs = ["access_log.h"],
    deps = [
        ":header_map_includes",
        ":protocol_includes",
        "//include/envoy/common:optional_includes",
        "//include/envoy/common:time_includes",
        "//include/envoy/upstream:upstream_includes",
    ],
)

envoy_cc_library(
    name = "async_client_includes",
    hdrs = ["async_client.h"],
    deps = [
        ":message_includes",
        "//include/envoy/common:optional_includes",
    ],
)

envoy_cc_library(
    name = "codec_includes",
    hdrs = ["codec.h"],
    deps = [
        ":header_map_includes",
        ":protocol_includes",
        "//include/envoy/buffer:buffer_includes",
    ],
)

envoy_cc_library(
    name = "codes_includes",
    hdrs = ["codes.h"],
)

envoy_cc_library(
    name = "conn_pool_includes",
    hdrs = ["conn_pool.h"],
    deps = [
        ":codec_includes",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/upstream:upstream_includes",
    ],
)

envoy_cc_library(
    name = "filter_includes",
    hdrs = ["filter.h"],
    deps = [
        ":access_log_includes",
        ":codec_includes",
        ":header_map_includes",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/router:router_includes",
        "//include/envoy/ssl:connection_includes",
    ],
)

envoy_cc_library(
    name = "header_map_includes",
    hdrs = ["header_map.h"],
)

envoy_cc_library(
    name = "message_includes",
    hdrs = ["message.h"],
    deps = [
        ":header_map_includes",
        "//include/envoy/buffer:buffer_includes",
    ],
)

envoy_cc_library(
    name = "protocol_includes",
    hdrs = ["protocol.h"],
)

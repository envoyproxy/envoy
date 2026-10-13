load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = envoy_select_enable_http3(["conn_pool.cc"]),
    hdrs = envoy_select_enable_http3(["conn_pool.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/event:dispatcher_interface",
        "//envoy/http:persistent_quic_info_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/http:conn_pool_base_lib",
        "//source/common/quic:client_connection_factory_lib",
        "@quiche//:quic_core_deterministic_connection_id_generator_lib",
    ]),
)

envoy_cc_library(
    name = "codec_stats_lib",
    hdrs = ["codec_stats.h"],
    deps = [
        "//envoy/http:header_validator_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:thread_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    external_deps = [
        "nghttp2",
        "abseil_optional",
    ],
    deps = [
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codec_helper_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:timespan",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_lib",
    ],
)

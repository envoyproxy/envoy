package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    external_deps = ["http_parser"],
    deps = [
        "//include/envoy/buffer:buffer_includes",
        "//include/envoy/http:codec_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/network:connection_includes",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
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
        "//include/envoy/common:optional_includes",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/event:timer_includes",
        "//include/envoy/http:conn_pool_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/upstream:upstream_includes",
        "//source/common/common:linked_object",
        "//source/common/common:utility_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:codec_wrappers_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:headers_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_lib",
    ],
)

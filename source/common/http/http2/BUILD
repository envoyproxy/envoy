load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_nghttp2",
)
load(
    "//bazel:envoy_internal.bzl",
    "envoy_external_dep_path",
)

licenses(["notice"])  # Apache 2

envoy_package()

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

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":codec_stats_lib",
        ":metadata_decoder_lib",
        ":metadata_encoder_lib",
        ":protocol_constraints_lib",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:header_map_interface",
        "//envoy/network:connection_interface",
        "//envoy/stats:stats_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codec_helper_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:status_lib",
        "//source/common/http:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_google_quiche//:http2_adapter",
        "@com_google_absl//absl/algorithm",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ] + envoy_select_nghttp2([envoy_external_dep_path("nghttp2")]),
)

# Separate library for some nghttp2 setup stuff to avoid having tests take a
# dependency on everything in codec_lib.
envoy_cc_library(
    name = "nghttp2_lib",
    srcs = ["nghttp2.cc"],
    hdrs = ["nghttp2.h"],
    external_deps = ["nghttp2"],
    deps = [
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/http:conn_pool_base_lib",
    ],
)

envoy_cc_library(
    name = "metadata_encoder_lib",
    srcs = ["metadata_encoder.cc"],
    hdrs = ["metadata_encoder.h"],
    deps = [
        "//envoy/http:codec_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "@com_github_google_quiche//:http2_adapter",
    ],
)

envoy_cc_library(
    name = "metadata_decoder_lib",
    srcs = ["metadata_decoder.cc"],
    hdrs = ["metadata_decoder.h"],
    deps = [
        "//envoy/http:codec_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_google_quiche//:http2_hpack_decoder_hpack_decoder_lib",
    ] + envoy_select_nghttp2([envoy_external_dep_path("nghttp2")]),
)

envoy_cc_library(
    name = "protocol_constraints_lib",
    srcs = ["protocol_constraints.cc"],
    hdrs = ["protocol_constraints.h"],
    deps = [
        ":codec_stats_lib",
        "//envoy/network:connection_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/http:status_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ] + envoy_select_nghttp2([envoy_external_dep_path("nghttp2")]),
)

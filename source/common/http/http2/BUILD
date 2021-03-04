load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "codec_stats_lib",
    hdrs = ["codec_stats.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    external_deps = [
        "nghttp2",
        "abseil_optional",
        "abseil_inlined_vector",
        "abseil_algorithm",
    ],
    deps = [
        ":codec_stats_lib",
        ":metadata_decoder_lib",
        ":metadata_encoder_lib",
        ":protocol_constraints_lib",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
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
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
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
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/http:conn_pool_base_lib",
    ],
)

envoy_cc_library(
    name = "metadata_encoder_lib",
    srcs = ["metadata_encoder.cc"],
    hdrs = ["metadata_encoder.h"],
    external_deps = [
        "nghttp2",
    ],
    deps = [
        "//include/envoy/http:codec_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "metadata_decoder_lib",
    srcs = ["metadata_decoder.cc"],
    hdrs = ["metadata_decoder.h"],
    external_deps = [
        "nghttp2",
    ],
    deps = [
        "//include/envoy/http:codec_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "protocol_constraints_lib",
    srcs = ["protocol_constraints.cc"],
    hdrs = ["protocol_constraints.h"],
    external_deps = [
        "nghttp2",
    ],
    deps = [
        ":codec_stats_lib",
        "//include/envoy/network:connection_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/http:status_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

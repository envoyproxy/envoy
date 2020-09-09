load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

CODEC_TEST_DEPS = [
    ":codec_impl_test_util",
    "//source/common/event:dispatcher_lib",
    "//source/common/http:exception_lib",
    "//source/common/http:header_map_lib",
    "//source/common/http:header_utility_lib",
    "//source/common/http/http2:codec_legacy_lib",
    "//source/common/http/http2:codec_lib",
    "//source/common/runtime:runtime_lib",
    "//source/common/stats:stats_lib",
    "//test/common/http:common_lib",
    "//test/common/http/http2:http2_frame",
    "//test/common/stats:stat_test_utility_lib",
    "//test/mocks/http:http_mocks",
    "//test/mocks/init:init_mocks",
    "//test/mocks/local_info:local_info_mocks",
    "//test/mocks/network:network_mocks",
    "//test/mocks/protobuf:protobuf_mocks",
    "//test/mocks/thread_local:thread_local_mocks",
    "//test/mocks/upstream:transport_socket_match_mocks",
    "//test/mocks/upstream:upstream_mocks",
    "//test/test_common:logging_lib",
    "//test/test_common:registry_lib",
    "//test/test_common:test_runtime_lib",
    "//test/test_common:utility_lib",
]

envoy_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    # The default codec is the legacy codec. Override runtime flag for testing new codec.
    args = [
        "--runtime-feature-override-for-tests=envoy.reloadable_features.new_codec_behavior",
    ],
    shard_count = 5,
    deps = CODEC_TEST_DEPS,
)

envoy_cc_test(
    name = "codec_impl_legacy_test",
    srcs = ["codec_impl_test.cc"],
    # The default codec is the legacy codec. Verify the runtime flag for the new codec is disabled.
    args = [
        "--runtime-feature-disable-for-tests=envoy.reloadable_features.new_codec_behavior",
    ],
    shard_count = 5,
    deps = CODEC_TEST_DEPS,
)

envoy_cc_test_library(
    name = "codec_impl_test_util",
    hdrs = ["codec_impl_test_util.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/http/http2:codec_legacy_lib",
        "//source/common/http/http2:codec_lib",
    ],
)

envoy_cc_test(
    name = "conn_pool_test",
    srcs = ["conn_pool_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/http/http2:conn_pool_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/http:common_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:transport_socket_match_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test_library(
    name = "http2_frame",
    srcs = ["http2_frame.cc"],
    hdrs = ["http2_frame.h"],
    external_deps = [
        "nghttp2",
    ],
    deps = [
        "//include/envoy/http:metadata_interface_with_external_headers",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:macros",
    ],
)

envoy_cc_test_library(
    name = "frame_replay_lib",
    srcs = ["frame_replay.cc"],
    hdrs = ["frame_replay.h"],
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/common:macros",
        "//source/common/http:utility_lib",
        "//source/common/http/http2:codec_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "frame_replay_test",
    srcs = ["frame_replay_test.cc"],
    data = [
        "request_header_corpus/simple_example_huffman",
        "request_header_corpus/simple_example_plain",
        "response_header_corpus/simple_example_huffman",
        "response_header_corpus/simple_example_plain",
    ],
    deps = [
        ":frame_replay_lib",
        "//test/common/http/http2:codec_impl_test_util",
    ],
)

envoy_cc_test(
    name = "metadata_encoder_decoder_test",
    srcs = ["metadata_encoder_decoder_test.cc"],
    external_deps = [
        "nghttp2",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http/http2:metadata_decoder_lib",
        "//source/common/http/http2:metadata_encoder_lib",
        "//source/common/runtime:runtime_lib",
        "//test/common/http/http2:http2_frame",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "http2_frame_test",
    srcs = ["http2_frame_test.cc"],
    deps = [
        "//include/envoy/http:metadata_interface",
        "//source/common/http/http2:metadata_encoder_lib",
        "//test/common/http/http2:http2_frame",
    ],
)

envoy_cc_fuzz_test(
    name = "response_header_fuzz_test",
    srcs = ["response_header_fuzz_test.cc"],
    corpus = "response_header_corpus",
    deps = [
        ":frame_replay_lib",
        "//test/common/http/http2:codec_impl_test_util",
    ],
)

envoy_cc_fuzz_test(
    name = "request_header_fuzz_test",
    srcs = ["request_header_fuzz_test.cc"],
    corpus = "request_header_corpus",
    deps = [
        ":frame_replay_lib",
        "//test/common/http/http2:codec_impl_test_util",
    ],
)

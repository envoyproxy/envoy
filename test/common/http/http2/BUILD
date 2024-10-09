load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "codec_impl_test",
    size = "large",
    srcs = ["codec_impl_test.cc"],
    rbe_pool = "2core",
    shard_count = 5,
    deps = [
        ":codec_impl_test_util",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/http/header_validators/envoy_default:header_validator_factory",
        "//source/extensions/http/header_validators/envoy_default:http2_header_validator",
        "//test/common/http:common_lib",
        "//test/common/http/http2:http2_frame",
        "//test/common/memory:memory_test_utility_lib",
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
        "@com_github_google_quiche//:http2_adapter",
    ],
)

envoy_cc_test_library(
    name = "codec_impl_test_util",
    hdrs = ["codec_impl_test_util.h"],
    deps = [
        "//source/common/http/http2:codec_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:overload_manager_mocks",
        "@com_github_google_quiche//:http2_adapter",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_test(
    name = "conn_pool_test",
    srcs = ["conn_pool_test.cc"],
    rbe_pool = "2core",
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
        "//test/mocks/http:http_server_properties_cache_mocks",
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
    deps = [
        "//envoy/http:metadata_interface_with_external_headers",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:macros",
        "@com_github_google_quiche//:http2_hpack_hpack_lib",
    ],
)

envoy_cc_test_library(
    name = "frame_replay_lib",
    srcs = ["frame_replay.cc"],
    hdrs = ["frame_replay.h"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/common:macros",
        "//source/common/http:utility_lib",
        "//source/common/http/http2:codec_lib",
        "//test/common/http:common_lib",
        "//test/mocks:common_lib",
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
    rbe_pool = "2core",
    deps = [
        ":frame_replay_lib",
        "//test/common/http/http2:codec_impl_test_util",
    ],
)

envoy_cc_test(
    name = "metadata_encoder_test",
    srcs = ["metadata_encoder_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/http/http2:metadata_decoder_lib",
        "//source/common/http/http2:metadata_encoder_lib",
        "//source/common/runtime:runtime_lib",
        "//test/common/http/http2:http2_frame",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:http2_adapter",
        "@com_github_google_quiche//:http2_adapter_mock_http2_visitor",
    ],
)

envoy_cc_test(
    name = "http2_frame_test",
    srcs = ["http2_frame_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/http:metadata_interface",
        "//source/common/http/http2:metadata_encoder_lib",
        "//test/common/http/http2:http2_frame",
    ],
)

envoy_cc_test(
    name = "protocol_constraints_test",
    srcs = ["protocol_constraints_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http/http2:protocol_constraints_lib",
        "//test/common/memory:memory_test_utility_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "response_header_fuzz_test",
    srcs = ["response_header_fuzz_test.cc"],
    corpus = "response_header_corpus",
    rbe_pool = "2core",
    deps = [
        ":frame_replay_lib",
        "//test/common/http/http2:codec_impl_test_util",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "request_header_fuzz_test",
    srcs = ["request_header_fuzz_test.cc"],
    corpus = "request_header_corpus",
    rbe_pool = "2core",
    deps = [
        ":frame_replay_lib",
        "//test/common/http/http2:codec_impl_test_util",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_proto_library(
    name = "hpack_fuzz_proto",
    srcs = ["hpack_fuzz.proto"],
    deps = ["//test/fuzz:common_proto"],
)

envoy_cc_fuzz_test(
    name = "hpack_fuzz_test",
    srcs = ["hpack_fuzz_test.cc"],
    corpus = "hpack_corpus",
    external_deps = ["nghttp2"],
    rbe_pool = "2core",
    deps = [
        ":hpack_fuzz_proto_cc_proto",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "http2_connection_proto",
    srcs = ["http2_connection.proto"],
    deps = ["//test/fuzz:common_proto"],
)

envoy_cc_fuzz_test(
    name = "http2_connection_fuzz_test",
    srcs = ["http2_connection_fuzz_test.cc"],
    corpus = "http2_connection_corpus",
    external_deps = ["nghttp2"],
    rbe_pool = "2core",
    deps = [
        ":http2_connection_proto_cc_proto",
        "//source/common/http/http2:codec_lib",
        "//test/common/http/http2:http2_frame",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

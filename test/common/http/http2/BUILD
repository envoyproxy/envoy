licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    shard_count = 5,
    deps = [
        ":codec_impl_test_util",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/stats:stats_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "codec_impl_test_util",
    hdrs = ["codec_impl_test_util.h"],
    deps = [
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
        "//test/mocks/upstream:upstream_mocks",
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
        "//test/test_common:utility_lib",
    ],
)

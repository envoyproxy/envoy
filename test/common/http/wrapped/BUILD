licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "src_ip_transparent_mapper_test",
    srcs = ["src_ip_transparent_mapper_test.cc"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
        "//source/common/http/wrapped:src_ip_transparent_mapper_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/http:conn_pool_mocks",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
    ],
)

envoy_cc_test(
    name = "wrapped_connection_pool_test",
    srcs = ["wrapped_connection_pool_test.cc"],
    deps = [
        "//include/envoy/http:codec_interface",
        "//source/common/http/wrapped:wrapped_connection_pool_lib",
        "//test/common/http:common_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/http:conn_pool_mocks",
        "//test/mocks/http:mock_connection_mapper",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/http:stream_encoder_mock",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
    ],
)

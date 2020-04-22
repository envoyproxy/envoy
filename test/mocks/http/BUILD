licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "api_listener_mocks",
    srcs = ["api_listener.cc"],
    hdrs = ["api_listener.h"],
    deps = [
        "//include/envoy/http:api_listener_interface",
    ],
)

envoy_cc_mock(
    name = "conn_pool_mocks",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_mock(
    name = "http_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        ":conn_pool_mocks",
        ":stream_decoder_mock",
        ":stream_encoder_mock",
        ":stream_mock",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:async_client_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/ssl:connection_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/http:header_map_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_mock(
    name = "stream_mock",
    srcs = ["stream.cc"],
    hdrs = ["stream.h"],
    deps = [
        "//include/envoy/http:codec_interface",
    ],
)

envoy_cc_mock(
    name = "stream_decoder_mock",
    srcs = ["stream_decoder.cc"],
    hdrs = ["stream_decoder.h"],
    deps = [
        "//include/envoy/http:codec_interface",
    ],
)

envoy_cc_mock(
    name = "stream_encoder_mock",
    srcs = ["stream_encoder.cc"],
    hdrs = ["stream_encoder.h"],
    deps = [
        ":stream_mock",
        "//include/envoy/http:codec_interface",
    ],
)

envoy_cc_test(
    name = "http_mocks_test",
    srcs = ["mocks_test.cc"],
    deps = [
        ":http_mocks",
        "//include/envoy/http:header_map_interface",
        "//test/test_common:utility_lib",
    ],
)

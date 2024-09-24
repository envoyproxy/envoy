load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "http_server_properties_cache_mocks",
    srcs = ["http_server_properties_cache.cc"],
    hdrs = ["http_server_properties_cache.h"],
    deps = [
        "//envoy/http:http_server_properties_cache_interface",
    ],
)

envoy_cc_mock(
    name = "api_listener_mocks",
    srcs = ["api_listener.cc"],
    hdrs = ["api_listener.h"],
    deps = [
        "//envoy/http:api_listener_interface",
    ],
)

envoy_cc_mock(
    name = "conn_pool_mocks",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    rbe_pool = "2core",
    deps = [
        "//envoy/http:conn_pool_interface",
        "//test/mocks:common_lib",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_mock(
    name = "header_validator_mocks",
    hdrs = ["header_validator.h"],
    deps = [
        "//envoy/http:header_validator_interface",
        "//test/mocks:common_lib",
    ],
)

envoy_cc_mock(
    name = "http_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    rbe_pool = "2core",
    deps = [
        ":conn_pool_mocks",
        ":stream_decoder_mock",
        ":stream_encoder_mock",
        ":stream_mock",
        "//envoy/access_log:access_log_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:async_client_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:conn_pool_interface",
        "//envoy/http:filter_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/tracing:tracer_interface",
        "//source/common/http:conn_manager_config_interface",
        "//source/common/http:filter_manager_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:host_mocks",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_mock(
    name = "stream_mock",
    srcs = ["stream.cc"],
    hdrs = ["stream.h"],
    deps = [
        "//envoy/http:codec_interface",
        "//source/common/network:socket_lib",
    ],
)

envoy_cc_mock(
    name = "stateful_session_mock",
    srcs = ["stateful_session.cc"],
    hdrs = ["stateful_session.h"],
    deps = [
        "//envoy/http:stateful_session_interface",
    ],
)

envoy_cc_mock(
    name = "stream_decoder_mock",
    srcs = ["stream_decoder.cc"],
    hdrs = ["stream_decoder.h"],
    deps = [
        "//envoy/http:codec_interface",
    ],
)

envoy_cc_mock(
    name = "stream_encoder_mock",
    srcs = ["stream_encoder.cc"],
    hdrs = ["stream_encoder.h"],
    deps = [
        ":stream_mock",
        "//envoy/http:codec_interface",
        "//source/common/http:header_utility_lib",
    ],
)

envoy_cc_test(
    name = "http_mocks_test",
    srcs = ["mocks_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_mocks",
        "//envoy/http:header_map_interface",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_mock(
    name = "stream_reset_handler_mock",
    hdrs = ["stream_reset_handler.h"],
    deps = [
        "//envoy/http:stream_reset_handler_interface",
    ],
)

envoy_cc_mock(
    name = "early_header_mutation_mock",
    srcs = ["early_header_mutation.cc"],
    hdrs = ["early_header_mutation.h"],
    deps = [
        "//envoy/http:early_header_mutation_interface",
    ],
)

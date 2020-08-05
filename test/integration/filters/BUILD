load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "add_body_filter_config_lib",
    srcs = [
        "add_body_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "wait_for_whole_request_and_response_config_lib",
    srcs = [
        "wait_for_whole_request_and_response.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "add_trailers_filter_config_lib",
    srcs = [
        "add_trailers_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "backpressure_filter_config_lib",
    srcs = [
        "backpressure_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "clear_route_cache_filter_lib",
    srcs = [
        "clear_route_cache_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "eds_ready_filter_config_lib",
    srcs = [
        "eds_ready_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/stats:symbol_table_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "modify_buffer_filter_config_lib",
    srcs = [
        "modify_buffer_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "passthrough_filter_config_lib",
    srcs = [
        "passthrough_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "headers_only_filter_config_lib",
    srcs = [
        "headers_only_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "pause_filter_lib",
    srcs = [
        "pause_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "process_context_lib",
    srcs = [
        "process_context_filter.cc",
    ],
    hdrs = [
        "process_context_filter.h",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:process_context_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "stop_iteration_and_continue",
    srcs = [
        "stop_iteration_and_continue_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "common_lib",
    hdrs = [
        "common.h",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "request_metadata_filter_config_lib",
    srcs = [
        "request_metadata_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "random_pause_filter_lib",
    srcs = [
        "random_pause_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "response_metadata_filter_config_lib",
    srcs = [
        "response_metadata_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "decode_headers_return_stop_all_filter_config_lib",
    srcs = [
        "decode_headers_return_stop_all_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "call_decodedata_once_filter_config_lib",
    srcs = [
        "call_decodedata_once_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "encode_headers_return_stop_all_filter_config_lib",
    srcs = [
        "encode_headers_return_stop_all_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "metadata_stop_all_filter_config_lib",
    srcs = [
        "metadata_stop_all_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "encoder_decoder_buffer_filter_lib",
    srcs = [
        "encoder_decoder_buffer_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

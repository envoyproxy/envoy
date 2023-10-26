load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "add_body_filter_proto",
    srcs = ["add_body_filter.proto"],
)

envoy_proto_library(
    name = "add_header_filter_proto",
    srcs = ["add_header_filter.proto"],
)

envoy_proto_library(
    name = "test_listener_filter_proto",
    srcs = ["test_listener_filter.proto"],
)

envoy_cc_test_library(
    name = "test_listener_filter_lib",
    srcs = [
        "test_listener_filter.cc",
    ],
    hdrs = [
        "test_listener_filter.h",
    ],
    deps = [
        ":test_listener_filter_proto_cc_proto",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/router:string_accessor_lib",
    ],
)

envoy_cc_test_library(
    name = "add_body_filter_config_lib",
    srcs = [
        "add_body_filter.cc",
    ],
    deps = [
        ":add_body_filter_proto_cc_proto",
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "add_header_filter_config_lib",
    srcs = ["add_header_filter.cc"],
    deps = [
        ":add_header_filter_proto_cc_proto",
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
    alwayslink = 1,
)

envoy_cc_test_library(
    name = "header_to_proxy_filter_lib",
    srcs = [
        "header_to_proxy_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/network:filter_state_proxy_info_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "local_reply_during_encoding_filter_lib",
    srcs = [
        "local_reply_during_encoding_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "local_reply_during_decoding_filter_lib",
    srcs = [
        "local_reply_during_decoding_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "tee_filter_lib",
    srcs = [
        "tee_filter.cc",
    ],
    hdrs = [
        "tee_filter.h",
    ],
    external_deps = ["abseil_synchronization"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "local_reply_during_encoding_data_filter_lib",
    srcs = [
        "local_reply_during_encoding_data_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "continue_after_local_reply_filter_lib",
    srcs = [
        "continue_after_local_reply_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_proto_library(
    name = "crash_filter_proto",
    srcs = ["crash_filter.proto"],
)

envoy_cc_test_library(
    name = "crash_filter_config_lib",
    srcs = [
        "crash_filter.cc",
    ],
    deps = [
        ":common_lib",
        ":crash_filter_proto_cc_proto",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "local_reply_with_metadata_filter_lib",
    srcs = [
        "local_reply_with_metadata_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "continue_headers_only_inject_body",
    srcs = [
        "continue_headers_only_inject_body_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "stop_iteration_headers_inject_body",
    srcs = [
        "stop_iteration_headers_inject_body_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "async_inject_body_at_end_stream_filter_lib",
    srcs = [
        "async_inject_body_at_end_stream_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/http:utility_lib",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "add_invalid_data_filter_lib",
    srcs = [
        "add_invalid_data_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "assert_non_reentrant_filter_lib",
    srcs = [
        "assert_non_reentrant_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/buffer:buffer_lib",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/stats:symbol_table_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "modify_buffer_filter_config_lib",
    srcs = [
        "modify_buffer_filter.cc",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "on_local_reply_filter_config_lib",
    srcs = [
        "on_local_reply_filter.cc",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "reset_stream_filter_config_lib",
    srcs = [
        "reset_stream_filter.cc",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "pause_filter_for_quic_lib",
    srcs = [
        "pause_filter_for_quic.cc",
    ],
    tags = ["nofips"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//source/common/quic:quic_filter_manager_connection_lib",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:process_context_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "reset_idle_timer_filter_lib",
    srcs = [
        "reset_idle_timer_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "set_response_code_filter_lib",
    srcs = [
        "set_response_code_filter.cc",
    ],
    deps = [
        ":set_response_code_filter_config_proto_cc_proto",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_proto_library(
    name = "set_response_code_filter_config_proto",
    srcs = [":set_response_code_filter_config.proto"],
)

envoy_cc_test_library(
    name = "set_is_terminal_filter_lib",
    srcs = [
        "set_is_terminal_filter.cc",
    ],
    deps = [
        ":set_is_terminal_filter_config_proto_cc_proto",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_proto_library(
    name = "set_is_terminal_filter_config_proto",
    srcs = [":set_is_terminal_filter_config.proto"],
)

envoy_cc_test_library(
    name = "server_factory_context_filter_lib",
    srcs = [
        "server_factory_context_filter.cc",
    ],
    deps = [
        ":server_factory_context_filter_config_proto_cc_proto",
        "//envoy/grpc:async_client_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//source/common/grpc:typed_async_client_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/proto:helloworld_proto_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "server_factory_context_filter_config_proto",
    srcs = [":server_factory_context_filter_config.proto"],
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)

envoy_proto_library(
    name = "stop_and_continue_filter_config_proto",
    srcs = [":stop_and_continue_filter_config.proto"],
)

envoy_cc_test_library(
    name = "stop_iteration_and_continue",
    srcs = [
        "stop_iteration_and_continue_filter.cc",
    ],
    deps = [
        ":stop_and_continue_filter_config_proto_cc_proto",
        "//envoy/common:scope_tracker_interface",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//source/common/common:scope_tracker",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "common_lib",
    hdrs = [
        "common.h",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "encode1xx_local_reply_config_lib",
    srcs = [
        "encode1xx_local_reply_filter.cc",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/event:timer_interface",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/event:timer_interface",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "decode_dynamic_metadata_filter_lib",
    srcs = [
        "decode_dynamic_metadata_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/event:timer_interface",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "metadata_control_filter_lib",
    srcs = ["metadata_control_filter.cc"],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
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
        "//envoy/event:timer_interface",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
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
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "buffer_continue_filter_lib",
    srcs = [
        "buffer_continue_filter.cc",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "test_socket_interface_lib",
    srcs = [
        "test_socket_interface.cc",
    ],
    hdrs = [
        "test_socket_interface.h",
    ],
    deps = [
        "//envoy/network:socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
        "//test/test_common:network_utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/extensions/network/socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "invalid_header_filter_lib",
    srcs = [
        "invalid_header_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/http:header_utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "address_restore_listener_filter_lib",
    srcs = [
        "address_restore_listener_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:address_lib",
        "//source/common/network:upstream_socket_options_filter_state_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_proto_library(
    name = "inspect_data_listener_filter_config_proto",
    srcs = [":inspect_data_listener_filter_config.proto"],
)

envoy_cc_test_library(
    name = "inspect_data_listener_filter_lib",
    srcs = [
        "inspect_data_listener_filter.cc",
    ],
    deps = [
        ":inspect_data_listener_filter_config_proto_cc_proto",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_proto_library(
    name = "set_route_filter_config_proto",
    srcs = [":set_route_filter_config.proto"],
)

envoy_cc_test_library(
    name = "set_route_filter_lib",
    srcs = [
        "set_route_filter.cc",
    ],
    deps = [
        ":common_lib",
        ":set_route_filter_config_proto_cc_proto",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/router:delegating_route_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/test_common:delegating_route_utility_lib",
    ],
)

envoy_cc_test_library(
    name = "remove_response_headers_lib",
    srcs = [
        "remove_response_headers.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "repick_cluster_filter_lib",
    srcs = [
        "repick_cluster_filter.cc",
    ],
    hdrs = [
        "repick_cluster_filter.h",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "@com_google_absl//absl/strings:str_format",
    ],
)

envoy_cc_test_library(
    name = "listener_typed_metadata_filter_lib",
    srcs = [
        "listener_typed_metadata_filter.cc",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/network:listener_interface",
        "//envoy/registry",
        "//source/common/protobuf",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_test_library(
    name = "stream_info_to_headers_filter_lib",
    srcs = [
        "stream_info_to_headers_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_proto_library(
    name = "test_network_filter_proto",
    srcs = [":test_network_filter.proto"],
)

envoy_cc_test_library(
    name = "test_network_filter_lib",
    srcs = [
        "test_network_filter.cc",
    ],
    deps = [
        ":test_network_filter_proto_cc_proto",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/network/common:factory_base_lib",
    ],
)

envoy_proto_library(
    name = "test_network_async_tcp_proto",
    srcs = [":test_network_async_tcp_filter.proto"],
)

envoy_cc_test_library(
    name = "test_network_async_tcp_filter_lib",
    srcs = [
        "test_network_async_tcp_filter.cc",
    ],
    deps = [
        ":test_network_async_tcp_proto_cc_proto",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/tcp:async_tcp_client_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
    ],
)

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_cc_test_library",
)
load(
    "@envoy//bazel/external:quiche.bzl",
    "envoy_quic_cc_library",
    "envoy_quic_cc_test_library",
    "envoy_quiche_platform_impl_cc_library",
    "envoy_quiche_platform_impl_cc_test_library",
    "quiche_copts",
)
load("@rules_proto//proto:defs.bzl", "proto_library")

licenses(["notice"])  # Apache 2

# QUICHE is Google's implementation of QUIC and related protocols. It is the
# same code used in Chromium and Google's servers, but packaged in a form that
# is intended to be easier to incorporate into third-party projects.
#
# QUICHE code falls into three groups:
# 1. Platform-independent code. Most QUICHE code is in this category.
# 2. APIs and type aliases to platform-dependent code/types, referenced by code
#    in group 1. This group is called the "Platform API".
# 3. Definitions of types declared in group 2. This group is called the
#    "Platform impl", and must be provided by the codebase that embeds QUICHE.
#
# Concretely, header files in group 2 (the Platform API) #include header and
# source files in group 3 (the Platform impl). The #include path for these
# files is always "quiche_platform_impl/". The files in type 3 are placed in
# //source/common/quic/platform/ or //test/common/quic/platform/ and are
# defined with include_prefix set to "quiche_platform_impl".

src_files = glob([
    "**/*.h",
    "**/*.c",
    "**/*.cc",
    "**/*.inc",
    "**/*.proto",
])

test_suite(
    name = "ci_tests",
    tests = [
        "http2_adapter_event_forwarder_test",
        "http2_adapter_header_validator_test",
        "http2_adapter_impl_comparison_test",
        "http2_adapter_nghttp2_adapter_test",
        "http2_adapter_nghttp2_data_provider_test",
        "http2_adapter_nghttp2_session_test",
        "http2_adapter_nghttp2_util_test",
        "http2_adapter_oghttp2_adapter_test",
        "http2_adapter_oghttp2_session_test",
        "http2_adapter_oghttp2_util_test",
        "http2_adapter_recording_http2_visitor_test",
        "http2_adapter_window_manager_test",
        "http2_platform_api_test",
        "quiche_balsa_balsa_frame_test",
        "quiche_balsa_balsa_headers_test",
        "quiche_balsa_header_properties_test",
        "quiche_balsa_simple_buffer_test",
        "quiche_common_test",
        "quiche_http_header_block_test",
    ],
)

envoy_cc_test_library(
    name = "http2_test_tools_random",
    srcs = ["quiche/http2/test_tools/http2_random.cc"],
    hdrs = ["quiche/http2/test_tools/http2_random.h"],
    external_deps = ["ssl"],
    repository = "@envoy",
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_adapter_chunked_buffer",
    srcs = ["quiche/http2/adapter/chunked_buffer.cc"],
    hdrs = ["quiche/http2/adapter/chunked_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_circular_deque_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_chunked_buffer_test",
    srcs = ["quiche/http2/adapter/chunked_buffer_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_platform_test",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "http2_adapter_data_source",
    hdrs = ["quiche/http2/adapter/data_source.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "http2_adapter_event_forwarder",
    srcs = ["quiche/http2/adapter/event_forwarder.cc"],
    hdrs = ["quiche/http2/adapter/event_forwarder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_core_http2_deframer_lib",
        ":quiche_common_callbacks",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_event_forwarder_test",
    srcs = ["quiche/http2/adapter/event_forwarder_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_event_forwarder",
        ":http2_core_protocol_lib",
        ":http2_test_tools_mock_spdy_framer_visitor_lib",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_header_validator",
    srcs = [
        "quiche/http2/adapter/header_validator.cc",
        "quiche/http2/adapter/noop_header_validator.cc",
    ],
    hdrs = [
        "quiche/http2/adapter/header_validator.h",
        "quiche/http2/adapter/header_validator_base.h",
        "quiche/http2/adapter/noop_header_validator.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_header_validator_test",
    srcs = [
        "quiche/http2/adapter/header_validator_test.cc",
        "quiche/http2/adapter/noop_header_validator_test.cc",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_header_validator",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_http2_protocol",
    srcs = ["quiche/http2/adapter/http2_protocol.cc"],
    hdrs = ["quiche/http2/adapter/http2_protocol.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
    ],
)

envoy_cc_library(
    name = "http2_adapter_http2_util",
    srcs = ["quiche/http2/adapter/http2_util.cc"],
    hdrs = ["quiche/http2/adapter/http2_util.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":http2_core_protocol_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "http2_adapter_http2_visitor_interface",
    hdrs = ["quiche/http2/adapter/http2_visitor_interface.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_impl_comparison_test",
    srcs = ["quiche/http2/adapter/adapter_impl_comparison_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter",
        ":http2_adapter_http2_protocol",
        ":http2_adapter_recording_http2_visitor",
        ":http2_adapter_test_frame_sequence",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_interface_lib",
    hdrs = [
        "quiche/http2/adapter/http2_adapter.h",
        "quiche/http2/adapter/http2_session.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_data_source",
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test_library(
    name = "http2_adapter_mock_http2_visitor",
    hdrs = ["quiche/http2/adapter/mock_http2_visitor.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_visitor_interface",
        ":quiche_common_platform_export",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test_library(
    name = "http2_adapter_mock_nghttp2_callbacks",
    srcs = ["quiche/http2/adapter/mock_nghttp2_callbacks.cc"],
    hdrs = ["quiche/http2/adapter/mock_nghttp2_callbacks.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_nghttp2_include",
        ":http2_adapter_nghttp2_util",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_nghttp2_adapter",
    srcs = [
        "quiche/http2/adapter/nghttp2_adapter.cc",
        "quiche/http2/adapter/nghttp2_session.cc",
    ],
    hdrs = [
        "quiche/http2/adapter/nghttp2_adapter.h",
        "quiche/http2/adapter/nghttp2_session.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_data_source",
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_util",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_interface_lib",
        ":http2_adapter_nghttp2_callbacks",
        ":http2_adapter_nghttp2_data_provider",
        ":http2_adapter_nghttp2_include",
        ":http2_adapter_nghttp2_util",
        ":http2_adapter_window_manager",
        ":http2_core_http2_trace_logging_lib",
        ":http2_core_priority_write_scheduler_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_nghttp2_adapter_test",
    srcs = ["quiche/http2/adapter/nghttp2_adapter_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_mock_http2_visitor",
        ":http2_adapter_nghttp2_adapter",
        ":http2_adapter_nghttp2_include",
        ":http2_adapter_nghttp2_test_utils",
        ":http2_adapter_oghttp2_util",
        ":http2_adapter_test_frame_sequence",
        ":http2_adapter_test_utils",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_nghttp2_callbacks",
    srcs = ["quiche/http2/adapter/nghttp2_callbacks.cc"],
    hdrs = ["quiche/http2/adapter/nghttp2_callbacks.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_data_source",
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_nghttp2_data_provider",
        ":http2_adapter_nghttp2_include",
        ":http2_adapter_nghttp2_util",
        ":quiche_common_platform",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "http2_adapter_nghttp2_data_provider",
    srcs = ["quiche/http2/adapter/nghttp2_data_provider.cc"],
    hdrs = ["quiche/http2/adapter/nghttp2_data_provider.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_data_source",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_nghttp2_include",
        ":http2_adapter_nghttp2_util",
    ],
)

envoy_cc_test(
    name = "http2_adapter_nghttp2_data_provider_test",
    srcs = ["quiche/http2/adapter/nghttp2_data_provider_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_nghttp2_data_provider",
        ":http2_adapter_test_utils",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_nghttp2_include",
    hdrs = ["quiche/http2/adapter/nghttp2.h"],
    copts = quiche_copts,
    external_deps = ["nghttp2"],
    repository = "@envoy",
)

envoy_cc_test(
    name = "http2_adapter_nghttp2_session_test",
    srcs = ["quiche/http2/adapter/nghttp2_session_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter",
        ":http2_adapter_mock_http2_visitor",
        ":http2_adapter_nghttp2_callbacks",
        ":http2_adapter_nghttp2_util",
        ":http2_adapter_test_frame_sequence",
        ":http2_adapter_test_utils",
        ":quiche_common_platform_expect_bug",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test_library(
    name = "http2_adapter_nghttp2_test_utils",
    srcs = ["quiche/http2/adapter/nghttp2_test_utils.cc"],
    hdrs = ["quiche/http2/adapter/nghttp2_test_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_nghttp2_include",
        ":http2_adapter_nghttp2_util",
        ":quiche_common_platform_export",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_nghttp2_util",
    srcs = ["quiche/http2/adapter/nghttp2_util.cc"],
    hdrs = ["quiche/http2/adapter/nghttp2_util.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_adapter_data_source",
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_nghttp2_include",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_nghttp2_util_test",
    srcs = ["quiche/http2/adapter/nghttp2_util_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_nghttp2_test_utils",
        ":http2_adapter_nghttp2_util",
        ":http2_adapter_test_utils",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_oghttp2_adapter",
    srcs = [
        "quiche/http2/adapter/oghttp2_adapter.cc",
        "quiche/http2/adapter/oghttp2_session.cc",
    ],
    hdrs = [
        "quiche/http2/adapter/oghttp2_adapter.h",
        "quiche/http2/adapter/oghttp2_session.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_adapter_chunked_buffer",
        ":http2_adapter_data_source",
        ":http2_adapter_event_forwarder",
        ":http2_adapter_header_validator",
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_util",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_interface_lib",
        ":http2_adapter_oghttp2_util",
        ":http2_adapter_window_manager",
        ":http2_core_framer_lib",
        ":http2_core_http2_deframer_lib",
        ":http2_core_http2_trace_logging_lib",
        ":http2_core_priority_write_scheduler_lib",
        ":http2_core_protocol_lib",
        ":http2_header_byte_listener_interface_lib",
        ":http2_no_op_headers_handler_lib",
        ":quiche_common_callbacks",
        "@com_google_absl//absl/algorithm",
        "@com_google_absl//absl/cleanup",
    ],
)

envoy_cc_test(
    name = "http2_adapter_oghttp2_adapter_test",
    srcs = ["quiche/http2/adapter/oghttp2_adapter_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_mock_http2_visitor",
        ":http2_adapter_oghttp2_adapter",
        ":http2_adapter_oghttp2_util",
        ":http2_adapter_test_frame_sequence",
        ":http2_adapter_test_utils",
        ":quiche_common_platform_expect_bug",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test(
    name = "http2_adapter_oghttp2_session_test",
    srcs = ["quiche/http2/adapter/oghttp2_session_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_mock_http2_visitor",
        ":http2_adapter_oghttp2_adapter",
        ":http2_adapter_test_frame_sequence",
        ":http2_adapter_test_utils",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_oghttp2_util",
    srcs = ["quiche/http2/adapter/oghttp2_util.cc"],
    hdrs = ["quiche/http2/adapter/oghttp2_util.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_adapter_http2_protocol",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_oghttp2_util_test",
    srcs = ["quiche/http2/adapter/oghttp2_util_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_oghttp2_util",
        ":http2_adapter_test_frame_sequence",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test_library(
    name = "http2_adapter_recording_http2_visitor",
    srcs = ["quiche/http2/adapter/recording_http2_visitor.cc"],
    hdrs = ["quiche/http2/adapter/recording_http2_visitor.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_util",
        ":http2_adapter_http2_visitor_interface",
        ":quiche_common_platform_export",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test(
    name = "http2_adapter_recording_http2_visitor_test",
    srcs = ["quiche/http2/adapter/recording_http2_visitor_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_recording_http2_visitor",
        ":http2_test_tools_random",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test_library(
    name = "http2_adapter_test_frame_sequence",
    srcs = ["quiche/http2/adapter/test_frame_sequence.cc"],
    hdrs = ["quiche/http2/adapter/test_frame_sequence.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_util",
        ":http2_adapter_oghttp2_util",
        ":http2_core_framer_lib",
        ":http2_core_protocol_lib",
        ":http2_hpack_hpack_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test_library(
    name = "http2_adapter_test_utils",
    srcs = ["quiche/http2/adapter/test_utils.cc"],
    hdrs = ["quiche/http2/adapter/test_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_adapter_chunked_buffer",
        ":http2_adapter_data_source",
        ":http2_adapter_http2_protocol",
        ":http2_adapter_http2_visitor_interface",
        ":http2_adapter_mock_http2_visitor",
        ":http2_core_protocol_lib",
        ":http2_hpack_hpack_lib",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test_library(
    name = "http2_adapter_test_utils_test",
    srcs = ["quiche/http2/adapter/test_utils_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_test_utils",
        ":http2_core_framer_lib",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_adapter_window_manager",
    srcs = ["quiche/http2/adapter/window_manager.cc"],
    hdrs = ["quiche/http2/adapter/window_manager.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_callbacks",
        ":quiche_common_platform",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test(
    name = "http2_adapter_window_manager_test",
    srcs = ["quiche/http2/adapter/window_manager_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_adapter_window_manager",
        ":http2_test_tools_random",
        ":quiche_common_platform_expect_bug",
        ":quiche_common_platform_export",
        ":quiche_common_platform_test",
        "@com_google_absl//absl/functional:bind_front",
    ],
)

envoy_cc_library(
    name = "http2_adapter",
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":http2_adapter_nghttp2_adapter",
        ":http2_adapter_oghttp2_adapter",
    ],
)

envoy_cc_library(
    name = "http2_core_priority_write_scheduler_lib",
    hdrs = ["quiche/http2/core/priority_write_scheduler.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_core_protocol_lib",
        ":quiche_common_circular_deque_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_core_http2_trace_logging_lib",
    srcs = ["quiche/http2/core/http2_trace_logging.cc"],
    hdrs = ["quiche/http2/core/http2_trace_logging.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_core_headers_handler_interface_lib",
        ":http2_core_http2_deframer_lib",
        ":http2_core_protocol_lib",
        ":http2_core_recording_headers_handler_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_constants_lib",
    srcs = ["quiche/http2/http2_constants.cc"],
    hdrs = ["quiche/http2/http2_constants.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_platform",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_cc_library(
    name = "http2_structures_lib",
    srcs = ["quiche/http2/http2_structures.cc"],
    hdrs = ["quiche/http2/http2_structures.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_decode_buffer_lib",
    srcs = ["quiche/http2/decoder/decode_buffer.cc"],
    hdrs = ["quiche/http2/decoder/decode_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_decoder_decode_http2_structures_lib",
    srcs = ["quiche/http2/decoder/decode_http2_structures.cc"],
    hdrs = ["quiche/http2/decoder/decode_http2_structures.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_decode_status_lib",
    srcs = ["quiche/http2/decoder/decode_status.cc"],
    hdrs = ["quiche/http2/decoder/decode_status.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_decoder_frame_decoder_state_lib",
    srcs = ["quiche/http2/decoder/frame_decoder_state.cc"],
    hdrs = ["quiche/http2/decoder/frame_decoder_state.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_structure_decoder_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_frame_decoder_lib",
    srcs = ["quiche/http2/decoder/http2_frame_decoder.cc"],
    hdrs = [
        "quiche/http2/decoder/frame_decoder_state.h",
        "quiche/http2/decoder/http2_frame_decoder.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_decoder_payload_decoders_altsvc_payload_decoder_lib",
        ":http2_decoder_payload_decoders_continuation_payload_decoder_lib",
        ":http2_decoder_payload_decoders_data_payload_decoder_lib",
        ":http2_decoder_payload_decoders_goaway_payload_decoder_lib",
        ":http2_decoder_payload_decoders_headers_payload_decoder_lib",
        ":http2_decoder_payload_decoders_ping_payload_decoder_lib",
        ":http2_decoder_payload_decoders_priority_payload_decoder_lib",
        ":http2_decoder_payload_decoders_priority_update_payload_decoder_lib",
        ":http2_decoder_payload_decoders_push_promise_payload_decoder_lib",
        ":http2_decoder_payload_decoders_rst_stream_payload_decoder_lib",
        ":http2_decoder_payload_decoders_settings_payload_decoder_lib",
        ":http2_decoder_payload_decoders_unknown_payload_decoder_lib",
        ":http2_decoder_payload_decoders_window_update_payload_decoder_lib",
        ":http2_decoder_structure_decoder_lib",
        ":http2_hpack_varint_hpack_varint_decoder_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_frame_decoder_listener_lib",
    srcs = ["quiche/http2/decoder/http2_frame_decoder_listener.cc"],
    hdrs = ["quiche/http2/decoder/http2_frame_decoder_listener.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_structures_lib",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_altsvc_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/altsvc_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/altsvc_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_continuation_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/continuation_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/continuation_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_data_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/data_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/data_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_goaway_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/goaway_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/goaway_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_headers_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/headers_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/headers_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_ping_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/ping_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/ping_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_priority_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/priority_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/priority_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_push_promise_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/push_promise_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/push_promise_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_rst_stream_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/rst_stream_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/rst_stream_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_settings_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/settings_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/settings_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_unknown_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/unknown_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/unknown_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_priority_update_payload_decoder_lib",
    srcs = [
        "quiche/http2/decoder/payload_decoders/priority_update_payload_decoder.cc",
    ],
    hdrs = [
        "quiche/http2/decoder/payload_decoders/priority_update_payload_decoder.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "http2_decoder_payload_decoders_window_update_payload_decoder_lib",
    srcs = ["quiche/http2/decoder/payload_decoders/window_update_payload_decoder.cc"],
    hdrs = ["quiche/http2/decoder/payload_decoders/window_update_payload_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_http2_structures_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_decoder_frame_decoder_state_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_structure_decoder_lib",
    srcs = ["quiche/http2/decoder/http2_structure_decoder.cc"],
    hdrs = ["quiche/http2/decoder/http2_structure_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_http2_structures_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_block_decoder_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_block_decoder.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_block_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_decoder_hpack_decoding_error_lib",
        ":http2_hpack_decoder_hpack_entry_decoder_lib",
        ":http2_hpack_decoder_hpack_entry_decoder_listener_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_decoder_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_decoder.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_decoder_hpack_block_decoder_lib",
        ":http2_hpack_decoder_hpack_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_decoder_state_lib",
        ":http2_hpack_decoder_hpack_decoder_tables_lib",
        ":http2_hpack_decoder_hpack_decoding_error_lib",
        ":http2_hpack_decoder_hpack_whole_entry_buffer_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_decoder_listener_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_decoder_listener.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_decoder_listener.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_hpack_constants_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_decoder_state_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_decoder_state.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_decoder_state.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_hpack_decoder_hpack_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_decoder_string_buffer_lib",
        ":http2_hpack_decoder_hpack_decoder_tables_lib",
        ":http2_hpack_decoder_hpack_decoding_error_lib",
        ":http2_hpack_decoder_hpack_whole_entry_listener_lib",
        ":http2_hpack_hpack_constants_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_decoder_string_buffer_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_decoder_string_buffer.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_decoder_string_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_huffman_hpack_huffman_decoder_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_decoder_tables_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_decoder_tables.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_decoder_tables.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_hpack_hpack_constants_lib",
        ":http2_hpack_hpack_static_table_entries_lib",
        ":quiche_common_circular_deque_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_decoding_error_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_decoding_error.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_decoding_error.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_entry_decoder_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_entry_decoder.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_entry_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_decoder_hpack_decoding_error_lib",
        ":http2_hpack_decoder_hpack_entry_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_entry_type_decoder_lib",
        ":http2_hpack_decoder_hpack_string_decoder_lib",
        ":http2_hpack_hpack_constants_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_entry_decoder_listener_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_entry_decoder_listener.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_entry_decoder_listener.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_hpack_constants_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_entry_type_decoder_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_entry_type_decoder.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_entry_type_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_hpack_constants_lib",
        ":http2_hpack_varint_hpack_varint_decoder_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_string_decoder_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_string_decoder.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_string_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_varint_hpack_varint_decoder_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_string_decoder_listener_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_string_decoder_listener.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_string_decoder_listener.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_whole_entry_buffer_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_whole_entry_buffer.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_whole_entry_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_decoder_hpack_decoder_string_buffer_lib",
        ":http2_hpack_decoder_hpack_decoding_error_lib",
        ":http2_hpack_decoder_hpack_entry_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_whole_entry_listener_lib",
        ":http2_hpack_hpack_constants_lib",
        ":quiche_common_platform",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_whole_entry_listener_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_whole_entry_listener.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_whole_entry_listener.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_decoder_hpack_decoder_string_buffer_lib",
        ":http2_hpack_decoder_hpack_decoding_error_lib",
        ":http2_hpack_hpack_constants_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_huffman_hpack_huffman_decoder_lib",
    srcs = ["quiche/http2/hpack/huffman/hpack_huffman_decoder.cc"],
    hdrs = ["quiche/http2/hpack/huffman/hpack_huffman_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_hpack_huffman_hpack_huffman_encoder_lib",
    srcs = ["quiche/http2/hpack/huffman/hpack_huffman_encoder.cc"],
    hdrs = ["quiche/http2/hpack/huffman/hpack_huffman_encoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_huffman_huffman_spec_tables_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_huffman_huffman_spec_tables_lib",
    srcs = ["quiche/http2/hpack/huffman/huffman_spec_tables.cc"],
    hdrs = ["quiche/http2/hpack/huffman/huffman_spec_tables.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_hpack_constants_lib",
    srcs = ["quiche/http2/hpack/http2_hpack_constants.cc"],
    hdrs = ["quiche/http2/hpack/http2_hpack_constants.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_hpack_hpack_static_table_entries_lib",
    hdrs = ["quiche/http2/hpack/hpack_static_table_entries.inc"],
    repository = "@envoy",
)

envoy_cc_library(
    name = "http2_hpack_varint_hpack_varint_decoder_lib",
    srcs = ["quiche/http2/hpack/varint/hpack_varint_decoder.cc"],
    hdrs = ["quiche/http2/hpack/varint/hpack_varint_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_varint_hpack_varint_encoder_lib",
    srcs = ["quiche/http2/hpack/varint/hpack_varint_encoder.cc"],
    hdrs = ["quiche/http2/hpack/varint/hpack_varint_encoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_no_op_headers_handler_lib",
    hdrs = ["quiche/http2/core/no_op_headers_handler.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":http2_header_byte_listener_interface_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_header_byte_listener_interface_lib",
    hdrs = ["quiche/http2/core/header_byte_listener_interface.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_core_alt_svc_wire_format_lib",
    srcs = ["quiche/http2/core/spdy_alt_svc_wire_format.cc"],
    hdrs = [
        "quiche/http2/core/spdy_alt_svc_wire_format.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_core_framer_lib",
    srcs = [
        "quiche/http2/core/spdy_frame_builder.cc",
        "quiche/http2/core/spdy_framer.cc",
    ],
    hdrs = [
        "quiche/http2/core/spdy_frame_builder.h",
        "quiche/http2/core/spdy_framer.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_core_alt_svc_wire_format_lib",
        ":http2_core_headers_handler_interface_lib",
        ":http2_core_protocol_lib",
        ":http2_core_zero_copy_output_buffer_lib",
        ":http2_hpack_hpack_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "common_http_http_header_block_lib",
    hdrs = ["quiche/common/http/http_header_block.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_lib",
        ":quiche_common_platform",
        ":quiche_common_text_utils_lib",
        ":quiche_http_header_block_lib",
    ],
)

envoy_cc_library(
    name = "http2_core_headers_handler_interface_lib",
    hdrs = [
        "quiche/http2/core/spdy_headers_handler_interface.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [":quiche_common_platform"],
)

envoy_cc_library(
    name = "http2_core_http2_deframer_lib",
    srcs = ["quiche/http2/core/http2_frame_decoder_adapter.cc"],
    hdrs = ["quiche/http2/core/http2_frame_decoder_adapter.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_constants_lib",
        ":http2_core_alt_svc_wire_format_lib",
        ":http2_core_headers_handler_interface_lib",
        ":http2_core_protocol_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_hpack_hpack_decoder_adapter_lib",
        ":http2_hpack_hpack_lib",
        ":http2_structures_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "quiche_common_intrusive_list_lib",
    hdrs = ["quiche/common/quiche_intrusive_list.h"],
    repository = "@envoy",
)

envoy_cc_library(
    name = "http2_hpack_hpack_lib",
    srcs = [
        "quiche/http2/hpack/hpack_constants.cc",
        "quiche/http2/hpack/hpack_encoder.cc",
        "quiche/http2/hpack/hpack_entry.cc",
        "quiche/http2/hpack/hpack_header_table.cc",
        "quiche/http2/hpack/hpack_output_stream.cc",
        "quiche/http2/hpack/hpack_static_table.cc",
    ],
    hdrs = [
        "quiche/http2/hpack/hpack_constants.h",
        "quiche/http2/hpack/hpack_encoder.h",
        "quiche/http2/hpack/hpack_entry.h",
        "quiche/http2/hpack/hpack_header_table.h",
        "quiche/http2/hpack/hpack_output_stream.h",
        "quiche/http2/hpack/hpack_static_table.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":http2_core_protocol_lib",
        ":http2_hpack_huffman_hpack_huffman_encoder_lib",
        ":quiche_common_callbacks",
        ":quiche_common_circular_deque_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_hpack_decoder_adapter_lib",
    srcs = ["quiche/http2/hpack/hpack_decoder_adapter.cc"],
    hdrs = ["quiche/http2/hpack/hpack_decoder_adapter.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_core_headers_handler_interface_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_decoder_hpack_decoder_lib",
        ":http2_hpack_decoder_hpack_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_decoder_tables_lib",
        ":http2_hpack_hpack_constants_lib",
        ":http2_hpack_hpack_lib",
        ":http2_no_op_headers_handler_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_test_library(
    name = "http2_test_tools_mock_spdy_framer_visitor_lib",
    srcs = ["quiche/http2/test_tools/mock_spdy_framer_visitor.cc"],
    hdrs = ["quiche/http2/test_tools/mock_spdy_framer_visitor.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_core_http2_deframer_lib",
        ":http2_core_recording_headers_handler_lib",
        ":http2_test_tools_test_utils_lib",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "http2_core_protocol_lib",
    srcs = ["quiche/http2/core/spdy_protocol.cc"],
    hdrs = [
        "quiche/http2/core/spdy_bitmasks.h",
        "quiche/http2/core/spdy_protocol.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_core_alt_svc_wire_format_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "http2_core_recording_headers_handler_lib",
    srcs = ["quiche/http2/core/recording_headers_handler.cc"],
    hdrs = ["quiche/http2/core/recording_headers_handler.h"],
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_core_headers_handler_interface_lib",
    ],
)

envoy_cc_test_library(
    name = "http2_test_tools_test_utils_lib",
    srcs = ["quiche/http2/test_tools/spdy_test_utils.cc"],
    hdrs = ["quiche/http2/test_tools/spdy_test_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_core_headers_handler_interface_lib",
        ":http2_core_protocol_lib",
        ":quiche_common_platform",
        ":quiche_common_test_tools_test_utils_lib",
    ],
)

envoy_cc_library(
    name = "http2_core_zero_copy_output_buffer_lib",
    hdrs = ["quiche/http2/core/zero_copy_output_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
)

envoy_cc_library(
    name = "quic_platform",
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_time_lib",
        ":quic_platform_base",
        ":quic_platform_hostname_utils",
        ":quic_platform_mutex",
    ],
)

envoy_cc_library(
    name = "quic_platform_hostname_utils",
    hdrs = [
        "quiche/quic/platform/api/quic_hostname_utils.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_hostname_utils",
    ],
)

envoy_cc_library(
    name = "quic_platform_stack_trace",
    hdrs = [
        "quiche/quic/platform/api/quic_stack_trace.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_stack_trace",
    ],
)

envoy_cc_library(
    name = "quic_platform_server_stats",
    hdrs = [
        "quiche/quic/platform/api/quic_server_stats.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_server_stats",
    ],
)

envoy_cc_library(
    name = "quic_platform_mutex",
    hdrs = [
        "quiche/quic/platform/api/quic_mutex.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_mutex",
    ],
)

envoy_cc_library(
    name = "quic_platform_base",
    hdrs = [
        "quiche/quic/platform/api/quic_client_stats.h",
        "quiche/quic/platform/api/quic_exported_stats.h",
        "quiche/quic/platform/api/quic_flag_utils.h",
        "quiche/quic/platform/api/quic_flags.h",
        "quiche/quic/platform/api/quic_logging.h",
        "quiche/quic/platform/api/quic_testvalue.h",
        # TODO: uncomment the following files as implementations are added.
        # "quiche/quic/platform/api/quic_fuzzed_data_provider.h",
        # "quiche/quic/platform/api/quic_test_loopback.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_bug_tracker",
        ":quic_platform_server_stats",
        ":quic_platform_stack_trace",
        ":quiche_common_buffer_allocator_lib",
        ":quiche_common_lib",
        ":quiche_common_platform_client_stats",
        ":quiche_common_platform_export",
        ":quiche_common_platform_server_stats",
        ":quiche_common_platform_testvalue",
        "@envoy//source/common/quic/platform:quic_base_impl_lib",
    ],
)

envoy_cc_library(
    name = "quic_platform_bug_tracker",
    hdrs = [
        "quiche/quic/platform/api/quic_bug_tracker.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_bug_tracker",
    ],
)

envoy_cc_library(
    name = "quic_platform_export",
    hdrs = ["quiche/quic/platform/api/quic_export.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_expect_bug",
    hdrs = ["quiche/quic/platform/api/quic_expect_bug.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_expect_bug"],
)

envoy_cc_library(
    name = "quic_platform_ip_address_family",
    hdrs = ["quiche/quic/platform/api/quic_ip_address_family.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_bug_tracker",
        ":quiche_common_ip_address_family",
    ],
)

envoy_cc_library(
    name = "quiche_common_ip_address_family",
    srcs = ["quiche/common/quiche_ip_address_family.cc"],
    hdrs = ["quiche/common/quiche_ip_address_family.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_bug_tracker",
    ],
)

envoy_cc_library(
    name = "quic_platform_ip_address",
    hdrs = ["quiche/quic/platform/api/quic_ip_address.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        ":quic_platform_export",
        ":quiche_common_ip_address",
    ],
)

envoy_cc_library(
    name = "quiche_common_ip_address",
    srcs = ["quiche/common/quiche_ip_address.cc"],
    hdrs = ["quiche/common/quiche_ip_address.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        ":quic_platform_export",
        ":quic_platform_ip_address_family",
    ],
)

envoy_cc_library(
    name = "quic_platform_udp_socket_platform",
    hdrs = select({
        "@envoy//bazel:linux": ["quiche/quic/platform/api/quic_udp_socket_platform_api.h"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_udp_socket_platform"],
)

envoy_cc_library(
    name = "quic_platform_socket_address",
    srcs = ["quiche/quic/platform/api/quic_socket_address.cc"],
    hdrs = ["quiche/quic/platform/api/quic_socket_address.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_export",
        ":quic_platform_ip_address",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_test",
    hdrs = ["quiche/quic/platform/api/quic_test.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_platform_base",
        ":quiche_common_platform_test",
        "@envoy//test/common/quic/platform:quiche_test_impl_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_test_output",
    hdrs = ["quiche/quic/platform/api/quic_test_output.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_test_output"],
)

envoy_cc_test_library(
    name = "quic_platform_thread",
    hdrs = ["quiche/quic/platform/api/quic_thread.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_thread"],
)

#TODO(danzh) Figure out why using envoy_proto_library() fails.
proto_library(
    name = "quic_core_proto_cached_network_parameters_proto",
    srcs = ["quiche/quic/core/proto/cached_network_parameters.proto"],
)

cc_proto_library(
    name = "quic_core_proto_cached_network_parameters_proto_cc",
    deps = [":quic_core_proto_cached_network_parameters_proto"],
)

envoy_cc_library(
    name = "quic_core_proto_cached_network_parameters_proto_header",
    hdrs = ["quiche/quic/core/proto/cached_network_parameters_proto.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_proto_cached_network_parameters_proto_cc"],
)

proto_library(
    name = "quic_core_proto_source_address_token_proto",
    srcs = ["quiche/quic/core/proto/source_address_token.proto"],
    deps = [":quic_core_proto_cached_network_parameters_proto"],
)

cc_proto_library(
    name = "quic_core_proto_source_address_token_proto_cc",
    deps = [":quic_core_proto_source_address_token_proto"],
)

envoy_cc_library(
    name = "quic_core_proto_source_address_token_proto_header",
    hdrs = ["quiche/quic/core/proto/source_address_token_proto.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_proto_source_address_token_proto_cc"],
)

proto_library(
    name = "quic_core_proto_crypto_server_config_proto",
    srcs = ["quiche/quic/core/proto/crypto_server_config.proto"],
)

cc_proto_library(
    name = "quic_core_proto_crypto_server_config_proto_cc",
    deps = [":quic_core_proto_crypto_server_config_proto"],
)

envoy_cc_library(
    name = "quic_core_proto_crypto_server_config_proto_header",
    hdrs = ["quiche/quic/core/proto/crypto_server_config_proto.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_proto_crypto_server_config_proto_cc"],
)

envoy_cc_library(
    name = "quic_core_ack_listener_interface_lib",
    srcs = ["quiche/quic/core/quic_ack_listener_interface.cc"],
    hdrs = ["quiche/quic/core/quic_ack_listener_interface.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_alarm_lib",
    srcs = ["quiche/quic/core/quic_alarm.cc"],
    hdrs = ["quiche/quic/core/quic_alarm.h"],
    deps = [
        ":quic_core_arena_scoped_ptr_lib",
        ":quic_core_connection_context_lib",
        ":quic_core_time_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_alarm_factory_lib",
    hdrs = ["quiche/quic/core/quic_alarm_factory.h"],
    deps = [
        ":quic_core_alarm_lib",
        ":quic_core_one_block_arena_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_bandwidth_lib",
    srcs = ["quiche/quic/core/quic_bandwidth.cc"],
    hdrs = ["quiche/quic/core/quic_bandwidth.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_batch_writer_batch_writer_buffer_lib",
    srcs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_batch_writer_buffer.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_batch_writer_buffer.h",
        ],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_linux_socket_utils_lib",
        ":quic_core_packet_writer_lib",
        ":quic_platform",
        ":quiche_common_circular_deque_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_batch_writer_batch_writer_base_lib",
    srcs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_batch_writer_base.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_batch_writer_base.h",
        ],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_batch_writer_batch_writer_buffer_lib",
        ":quic_core_packet_writer_lib",
        ":quic_core_types_lib",
        ":quic_platform",
    ],
)

envoy_cc_test_library(
    name = "quic_core_batch_writer_batch_writer_test_lib",
    hdrs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_batch_writer_test.h",
        ],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_batch_writer_batch_writer_base_lib",
        ":quic_core_udp_socket_lib",
        ":quic_platform_test",
    ],
)

envoy_cc_library(
    name = "quic_core_batch_writer_gso_batch_writer_lib",
    srcs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_gso_batch_writer.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_gso_batch_writer.h",
        ],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_batch_writer_batch_writer_base_lib",
        ":quic_core_linux_socket_utils_lib",
        ":quic_platform",
    ],
)

envoy_cc_library(
    name = "quic_core_batch_writer_sendmmsg_batch_writer_lib",
    srcs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_sendmmsg_batch_writer.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:linux": [
            "quiche/quic/core/batch_writer/quic_sendmmsg_batch_writer.h",
        ],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_batch_writer_batch_writer_base_lib",
        ":quic_core_linux_socket_utils_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_blocked_writer_interface_lib",
    hdrs = ["quiche/quic/core/quic_blocked_writer_interface.h"],
    tags = ["nofips"],
    deps = [":quic_platform_export"],
)

envoy_quic_cc_library(
    name = "quic_core_blocked_writer_list_lib",
    srcs = ["quiche/quic/core/quic_blocked_writer_list.cc"],
    hdrs = ["quiche/quic/core/quic_blocked_writer_list.h"],
    deps = [
        ":quic_core_blocked_writer_interface_lib",
        ":quic_platform_base",
        ":quic_platform_bug_tracker",
        ":quiche_common_lib",
    ],
)

envoy_cc_test(
    name = "quic_core_blocked_writer_list_test",
    srcs = ["quiche/quic/core/quic_blocked_writer_list_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_blocked_writer_interface_lib",
        ":quic_core_blocked_writer_list_lib",
        ":quic_platform_test",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_arena_scoped_ptr_lib",
    hdrs = ["quiche/quic/core/quic_arena_scoped_ptr.h"],
    deps = [":quic_platform_base"],
)

envoy_quic_cc_library(
    name = "quic_core_chaos_protector_lib",
    srcs = [
        "quiche/quic/core/quic_chaos_protector.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_chaos_protector.h",
    ],
    deps = [
        ":quic_core_crypto_random_lib",
        ":quic_core_data_lib",
        ":quic_core_framer_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_packets_lib",
        ":quic_core_stream_frame_data_producer_lib",
        ":quic_core_types_lib",
        ":quic_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_clock_lib",
    hdrs = ["quiche/quic/core/quic_clock.h"],
    deps = [
        ":quic_core_time_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_coalesced_packet_lib",
    srcs = ["quiche/quic/core/quic_coalesced_packet.cc"],
    hdrs = ["quiche/quic/core/quic_coalesced_packet.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quic_core_packets_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_config_lib",
    srcs = ["quiche/quic/core/quic_config.cc"],
    hdrs = ["quiche/quic/core/quic_config.h"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_packets_lib",
        ":quic_core_socket_address_coder_lib",
        ":quic_core_time_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_bandwidth_sampler_lib",
    srcs = ["quiche/quic/core/congestion_control/bandwidth_sampler.cc"],
    hdrs = ["quiche/quic/core/congestion_control/bandwidth_sampler.h"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_congestion_control_windowed_filter_lib",
        ":quic_core_packet_number_indexed_queue_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_bbr_lib",
    srcs = ["quiche/quic/core/congestion_control/bbr_sender.cc"],
    hdrs = ["quiche/quic/core/congestion_control/bbr_sender.h"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_congestion_control_bandwidth_sampler_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_congestion_control_windowed_filter_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_prague_sender_lib",
    srcs = [
        "quiche/quic/core/congestion_control/prague_sender.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/prague_sender.h",
    ],
    deps = [
        ":quic_core_clock_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_congestion_control_tcp_cubic_bytes_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_bbr2_lib",
    srcs = [
        "quiche/quic/core/congestion_control/bbr2_drain.cc",
        "quiche/quic/core/congestion_control/bbr2_misc.cc",
        "quiche/quic/core/congestion_control/bbr2_probe_bw.cc",
        "quiche/quic/core/congestion_control/bbr2_probe_rtt.cc",
        "quiche/quic/core/congestion_control/bbr2_sender.cc",
        "quiche/quic/core/congestion_control/bbr2_startup.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/bbr2_drain.h",
        "quiche/quic/core/congestion_control/bbr2_misc.h",
        "quiche/quic/core/congestion_control/bbr2_probe_bw.h",
        "quiche/quic/core/congestion_control/bbr2_probe_rtt.h",
        "quiche/quic/core/congestion_control/bbr2_sender.h",
        "quiche/quic/core/congestion_control/bbr2_startup.h",
    ],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_congestion_control_bandwidth_sampler_lib",
        ":quic_core_congestion_control_bbr_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_congestion_control_windowed_filter_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_platform",
        ":quiche_common_print_elements_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_general_loss_algorithm_lib",
    srcs = ["quiche/quic/core/congestion_control/general_loss_algorithm.cc"],
    hdrs = ["quiche/quic/core/congestion_control/general_loss_algorithm.h"],
    deps = [
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_congestion_control_interface_lib",
    hdrs = [
        "quiche/quic/core/congestion_control/loss_detection_interface.h",
        "quiche/quic/core/congestion_control/send_algorithm_interface.h",
    ],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_clock_lib",
        ":quic_core_config_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_congestion_control_lib",
    srcs = [
        "quiche/quic/core/congestion_control/send_algorithm_interface.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/loss_detection_interface.h",
        "quiche/quic/core/congestion_control/send_algorithm_interface.h",
    ],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_config_lib",
        ":quic_core_congestion_control_bbr2_lib",
        ":quic_core_congestion_control_bbr_lib",
        ":quic_core_congestion_control_prague_sender_lib",
        ":quic_core_congestion_control_tcp_cubic_bytes_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_pacing_sender_lib",
    srcs = ["quiche/quic/core/congestion_control/pacing_sender.cc"],
    hdrs = ["quiche/quic/core/congestion_control/pacing_sender.h"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_config_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_rtt_stats_lib",
    srcs = ["quiche/quic/core/congestion_control/rtt_stats.cc"],
    hdrs = ["quiche/quic/core/congestion_control/rtt_stats.h"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_tcp_cubic_helper",
    srcs = [
        "quiche/quic/core/congestion_control/hybrid_slow_start.cc",
        "quiche/quic/core/congestion_control/prr_sender.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/hybrid_slow_start.h",
        "quiche/quic/core/congestion_control/prr_sender.h",
    ],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_tcp_cubic_bytes_lib",
    srcs = [
        "quiche/quic/core/congestion_control/cubic_bytes.cc",
        "quiche/quic/core/congestion_control/tcp_cubic_sender_bytes.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/cubic_bytes.h",
        "quiche/quic/core/congestion_control/tcp_cubic_sender_bytes.h",
    ],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_congestion_control_tcp_cubic_helper",
        ":quic_core_connection_stats_lib",
        ":quic_core_constants_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_uber_loss_algorithm_lib",
    srcs = ["quiche/quic/core/congestion_control/uber_loss_algorithm.cc"],
    hdrs = ["quiche/quic/core/congestion_control/uber_loss_algorithm.h"],
    deps = [":quic_core_congestion_control_general_loss_algorithm_lib"],
)

envoy_quic_cc_library(
    name = "quic_core_congestion_control_windowed_filter_lib",
    hdrs = ["quiche/quic/core/congestion_control/windowed_filter.h"],
    deps = [":quic_core_time_lib"],
)

envoy_quic_cc_library(
    name = "quic_core_connection_context_lib",
    srcs = [
        "quiche/quic/core/quic_connection_context.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_connection_context.h",
    ],
    deps = [
        ":quic_platform_export",
        ":quiche_common_platform",
        ":quiche_common_text_utils_lib",
        "@com_google_absl//absl/strings:str_format",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_connection_id_manager",
    srcs = ["quiche/quic/core/quic_connection_id_manager.cc"],
    hdrs = ["quiche/quic/core/quic_connection_id_manager.h"],
    deps = [
        ":quic_core_alarm_factory_lib",
        ":quic_core_alarm_lib",
        ":quic_core_clock_lib",
        ":quic_core_connection_id_generator_interface_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_interval_set_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_connection_id_generator_interface_lib",
    hdrs = ["quiche/quic/core/connection_id_generator.h"],
    deps = [
        ":quic_core_types_lib",
        ":quic_core_versions_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_deterministic_connection_id_generator_lib",
    srcs = ["quiche/quic/core/deterministic_connection_id_generator.cc"],
    hdrs = ["quiche/quic/core/deterministic_connection_id_generator.h"],
    deps = [
        ":quic_core_connection_id_generator_interface_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_connection_lib",
    srcs = [
        "quiche/quic/core/quic_connection.cc",
        "quiche/quic/core/quic_connection_alarms.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_connection.h",
        "quiche/quic/core/quic_connection_alarms.h",
    ],
    deps = [
        ":quic_core_alarm_factory_lib",
        ":quic_core_alarm_lib",
        ":quic_core_bandwidth_lib",
        ":quic_core_blocked_writer_interface_lib",
        ":quic_core_config_lib",
        ":quic_core_connection_context_lib",
        ":quic_core_connection_id_manager",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_idle_network_detector_lib",
        ":quic_core_mtu_discovery_lib",
        ":quic_core_network_blackhole_detector_lib",
        ":quic_core_one_block_arena_lib",
        ":quic_core_packet_creator_lib",
        ":quic_core_packet_writer_lib",
        ":quic_core_packets_lib",
        ":quic_core_path_validator_lib",
        ":quic_core_ping_manager_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_sent_packet_manager_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_uber_received_packet_manager_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_connection_stats_lib",
    srcs = ["quiche/quic/core/quic_connection_stats.cc"],
    hdrs = ["quiche/quic/core/quic_connection_stats.h"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_accumulator_lib",
        ":quic_core_time_lib",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_constants_lib",
    srcs = ["quiche/quic/core/quic_constants.cc"],
    hdrs = ["quiche/quic/core/quic_constants.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_crypto_logging",
    srcs = [
        "quiche/common/quiche_crypto_logging.cc",
    ],
    hdrs = [
        "quiche/common/quiche_crypto_logging.h",
    ],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_logging",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_crypto_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/cert_compressor.cc",
        "quiche/quic/core/crypto/channel_id.cc",
        "quiche/quic/core/crypto/crypto_framer.cc",
        "quiche/quic/core/crypto/crypto_handshake.cc",
        "quiche/quic/core/crypto/crypto_handshake_message.cc",
        "quiche/quic/core/crypto/crypto_secret_boxer.cc",
        "quiche/quic/core/crypto/crypto_utils.cc",
        "quiche/quic/core/crypto/curve25519_key_exchange.cc",
        "quiche/quic/core/crypto/key_exchange.cc",
        "quiche/quic/core/crypto/p256_key_exchange.cc",
        "quiche/quic/core/crypto/quic_compressed_certs_cache.cc",
        "quiche/quic/core/crypto/transport_parameters.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/cert_compressor.h",
        "quiche/quic/core/crypto/channel_id.h",
        "quiche/quic/core/crypto/crypto_framer.h",
        "quiche/quic/core/crypto/crypto_handshake.h",
        "quiche/quic/core/crypto/crypto_handshake_message.h",
        "quiche/quic/core/crypto/crypto_message_parser.h",
        "quiche/quic/core/crypto/crypto_secret_boxer.h",
        "quiche/quic/core/crypto/crypto_utils.h",
        "quiche/quic/core/crypto/curve25519_key_exchange.h",
        "quiche/quic/core/crypto/key_exchange.h",
        "quiche/quic/core/crypto/p256_key_exchange.h",
        "quiche/quic/core/crypto/proof_verifier.h",
        "quiche/quic/core/crypto/quic_compressed_certs_cache.h",
        "quiche/quic/core/crypto/transport_parameters.h",
    ],
    external_deps = ["ssl"],
    tags = [
        "pg3",
    ],
    deps = [
        ":quic_core_clock_lib",
        ":quic_core_connection_context_lib",
        ":quic_core_crypto_certificate_view_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_hkdf_lib",
        ":quic_core_crypto_proof_source_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_crypto_tls_handshake_lib",
        ":quic_core_data_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_lru_cache_lib",
        ":quic_core_packets_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_proto_source_address_token_proto_header",
        ":quic_core_server_id_lib",
        ":quic_core_socket_address_coder_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform",
        "@envoy//bazel/foreign_cc:zlib",
    ],
)

envoy_quic_cc_library(
    name = "quic_client_crypto_crypto_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/quic_client_session_cache.cc",
        "quiche/quic/core/crypto/quic_crypto_client_config.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/quic_client_session_cache.h",
        "quiche/quic/core/crypto/quic_crypto_client_config.h",
    ],
    tags = [
        "pg3",
    ],
    deps = [
        ":quic_client_crypto_tls_handshake_lib",
        ":quic_core_crypto_client_proof_source_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quiche_common_platform_client_stats",
        "@envoy//bazel/foreign_cc:zlib",
    ],
)

envoy_quic_cc_library(
    name = "quic_server_crypto_crypto_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/quic_crypto_server_config.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/quic_crypto_server_config.h",
    ],
    external_deps = ["ssl"],
    tags = [
        "pg3",
    ],
    deps = [
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_proto_crypto_server_config_proto_header",
        ":quic_core_server_id_lib",
        ":quic_server_crypto_tls_handshake_lib",
        "@envoy//bazel/foreign_cc:zlib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_boring_utils_lib",
    hdrs = ["quiche/quic/core/crypto/boring_utils.h"],
    external_deps = ["ssl"],
    deps = [
        ":quic_platform_export",
        ":quiche_common_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_certificate_view_lib",
    srcs = ["quiche/quic/core/crypto/certificate_view.cc"],
    hdrs = ["quiche/quic/core/crypto/certificate_view.h"],
    external_deps = ["ssl"],
    deps = [
        ":quic_core_crypto_boring_utils_lib",
        ":quic_core_types_lib",
        ":quic_platform",
        ":quic_platform_ip_address",
        ":quiche_common_platform",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_encryption_lib",
    srcs = [
        "quiche/quic/core/crypto/aead_base_decrypter.cc",
        "quiche/quic/core/crypto/aead_base_encrypter.cc",
        "quiche/quic/core/crypto/aes_128_gcm_12_decrypter.cc",
        "quiche/quic/core/crypto/aes_128_gcm_12_encrypter.cc",
        "quiche/quic/core/crypto/aes_128_gcm_decrypter.cc",
        "quiche/quic/core/crypto/aes_128_gcm_encrypter.cc",
        "quiche/quic/core/crypto/aes_256_gcm_decrypter.cc",
        "quiche/quic/core/crypto/aes_256_gcm_encrypter.cc",
        "quiche/quic/core/crypto/aes_base_decrypter.cc",
        "quiche/quic/core/crypto/aes_base_encrypter.cc",
        "quiche/quic/core/crypto/chacha20_poly1305_decrypter.cc",
        "quiche/quic/core/crypto/chacha20_poly1305_encrypter.cc",
        "quiche/quic/core/crypto/chacha20_poly1305_tls_decrypter.cc",
        "quiche/quic/core/crypto/chacha20_poly1305_tls_encrypter.cc",
        "quiche/quic/core/crypto/chacha_base_decrypter.cc",
        "quiche/quic/core/crypto/chacha_base_encrypter.cc",
        "quiche/quic/core/crypto/null_decrypter.cc",
        "quiche/quic/core/crypto/null_encrypter.cc",
        "quiche/quic/core/crypto/quic_crypter.cc",
        "quiche/quic/core/crypto/quic_decrypter.cc",
        "quiche/quic/core/crypto/quic_encrypter.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/aead_base_decrypter.h",
        "quiche/quic/core/crypto/aead_base_encrypter.h",
        "quiche/quic/core/crypto/aes_128_gcm_12_decrypter.h",
        "quiche/quic/core/crypto/aes_128_gcm_12_encrypter.h",
        "quiche/quic/core/crypto/aes_128_gcm_decrypter.h",
        "quiche/quic/core/crypto/aes_128_gcm_encrypter.h",
        "quiche/quic/core/crypto/aes_256_gcm_decrypter.h",
        "quiche/quic/core/crypto/aes_256_gcm_encrypter.h",
        "quiche/quic/core/crypto/aes_base_decrypter.h",
        "quiche/quic/core/crypto/aes_base_encrypter.h",
        "quiche/quic/core/crypto/chacha20_poly1305_decrypter.h",
        "quiche/quic/core/crypto/chacha20_poly1305_encrypter.h",
        "quiche/quic/core/crypto/chacha20_poly1305_tls_decrypter.h",
        "quiche/quic/core/crypto/chacha20_poly1305_tls_encrypter.h",
        "quiche/quic/core/crypto/chacha_base_decrypter.h",
        "quiche/quic/core/crypto/chacha_base_encrypter.h",
        "quiche/quic/core/crypto/crypto_protocol.h",
        "quiche/quic/core/crypto/null_decrypter.h",
        "quiche/quic/core/crypto/null_encrypter.h",
        "quiche/quic/core/crypto/quic_crypter.h",
        "quiche/quic/core/crypto/quic_decrypter.h",
        "quiche/quic/core/crypto/quic_encrypter.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":quic_core_crypto_hkdf_lib",
        ":quic_core_data_lib",
        ":quic_core_packets_lib",
        ":quic_core_tag_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quiche_crypto_logging",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_hkdf_lib",
    srcs = ["quiche/quic/core/crypto/quic_hkdf.cc"],
    hdrs = ["quiche/quic/core/crypto/quic_hkdf.h"],
    deps = [
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_proof_source_lib",
    srcs = [
        "quiche/quic/core/crypto/proof_source.cc",
        "quiche/quic/core/crypto/quic_crypto_proof.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/proof_source.h",
        "quiche/quic/core/crypto/quic_crypto_proof.h",
    ],
    deps = [
        ":quic_core_crypto_certificate_view_lib",
        ":quic_core_packets_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_proof_source_x509_lib",
    srcs = ["quiche/quic/core/crypto/proof_source_x509.cc"],
    hdrs = ["quiche/quic/core/crypto/proof_source_x509.h"],
    external_deps = ["ssl"],
    deps = [
        ":quic_core_crypto_certificate_view_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_proof_source_lib",
        ":quic_core_data_lib",
        ":quic_platform_base",
        ":quiche_common_endian_lib",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:node_hash_map",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_client_proof_source_lib",
    srcs = [
        "quiche/quic/core/crypto/client_proof_source.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/client_proof_source.h",
    ],
    deps = [
        ":quic_core_crypto_proof_source_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_crypto_random_lib",
    hdrs = ["quiche/quic/core/crypto/quic_random.h"],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quiche_common_random_lib"],
)

envoy_quic_cc_library(
    name = "quic_core_crypto_tls_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/tls_connection.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/tls_connection.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":quic_core_crypto_proof_source_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_server_crypto_tls_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/tls_server_connection.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/tls_server_connection.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":quic_core_crypto_proof_source_lib",
        ":quic_core_crypto_tls_handshake_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_client_crypto_tls_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/tls_client_connection.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/tls_client_connection.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":quic_core_crypto_tls_handshake_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_buffer_allocator_lib",
    srcs = [
        "quiche/common/quiche_buffer_allocator.cc",
        "quiche/common/simple_buffer_allocator.cc",
    ],
    hdrs = [
        "quiche/common/quiche_buffer_allocator.h",
        "quiche/common/simple_buffer_allocator.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
        ":quiche_common_platform_iovec",
        ":quiche_common_platform_logging",
        ":quiche_common_platform_prefetch",
    ],
)

envoy_cc_library(
    name = "quiche_common_circular_deque_lib",
    hdrs = ["quiche/common/quiche_circular_deque.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_common_random_lib",
    srcs = ["quiche/common/quiche_random.cc"],
    hdrs = ["quiche/common/quiche_random.h"],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quiche_common_platform_logging"],
)

envoy_cc_library(
    name = "quiche_common_status_utils",
    hdrs = ["quiche/common/quiche_status_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "quiche_common_wire_serialization",
    hdrs = ["quiche/common/wire_serialization.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_buffer_allocator_lib",
        ":quiche_common_lib",
        ":quiche_common_platform_logging",
        ":quiche_common_status_utils",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "quiche_common_callbacks",
    hdrs = ["quiche/common/quiche_callbacks.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/functional:function_ref",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_data_lib",
    srcs = [
        "quiche/quic/core/quic_data_reader.cc",
        "quiche/quic/core/quic_data_writer.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_data_reader.h",
        "quiche/quic/core/quic_data_writer.h",
    ],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_packets_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_error_codes_lib",
    srcs = ["quiche/quic/core/quic_error_codes.cc"],
    hdrs = ["quiche/quic/core/quic_error_codes.h"],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_feature_flags_list_lib",
    hdrs = ["quiche/common/quiche_feature_flags_list.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "quiche_protocol_flags_list_lib",
    hdrs = ["quiche/common/quiche_protocol_flags_list.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
)

envoy_quic_cc_library(
    name = "quic_core_framer_lib",
    srcs = ["quiche/quic/core/quic_framer.cc"],
    hdrs = ["quiche/quic/core/quic_framer.h"],
    deps = [
        ":quic_core_connection_id_generator_interface_lib",
        ":quic_core_constants_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_data_lib",
        ":quic_core_packets_lib",
        ":quic_core_socket_address_coder_lib",
        ":quic_core_stream_frame_data_producer_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quiche_common_text_utils_lib",
        ":quiche_common_wire_serialization",
        "@com_google_absl//absl/cleanup",
    ],
)

envoy_cc_library(
    name = "quic_core_frames_frames_lib",
    srcs = [
        "quiche/quic/core/frames/quic_ack_frame.cc",
        "quiche/quic/core/frames/quic_ack_frequency_frame.cc",
        "quiche/quic/core/frames/quic_blocked_frame.cc",
        "quiche/quic/core/frames/quic_connection_close_frame.cc",
        "quiche/quic/core/frames/quic_crypto_frame.cc",
        "quiche/quic/core/frames/quic_frame.cc",
        "quiche/quic/core/frames/quic_goaway_frame.cc",
        "quiche/quic/core/frames/quic_handshake_done_frame.cc",
        "quiche/quic/core/frames/quic_max_streams_frame.cc",
        "quiche/quic/core/frames/quic_message_frame.cc",
        "quiche/quic/core/frames/quic_new_connection_id_frame.cc",
        "quiche/quic/core/frames/quic_new_token_frame.cc",
        "quiche/quic/core/frames/quic_padding_frame.cc",
        "quiche/quic/core/frames/quic_path_challenge_frame.cc",
        "quiche/quic/core/frames/quic_path_response_frame.cc",
        "quiche/quic/core/frames/quic_ping_frame.cc",
        "quiche/quic/core/frames/quic_reset_stream_at_frame.cc",
        "quiche/quic/core/frames/quic_retire_connection_id_frame.cc",
        "quiche/quic/core/frames/quic_rst_stream_frame.cc",
        "quiche/quic/core/frames/quic_stop_sending_frame.cc",
        "quiche/quic/core/frames/quic_stop_waiting_frame.cc",
        "quiche/quic/core/frames/quic_stream_frame.cc",
        "quiche/quic/core/frames/quic_streams_blocked_frame.cc",
        "quiche/quic/core/frames/quic_window_update_frame.cc",
    ],
    hdrs = [
        "quiche/quic/core/frames/quic_ack_frame.h",
        "quiche/quic/core/frames/quic_ack_frequency_frame.h",
        "quiche/quic/core/frames/quic_blocked_frame.h",
        "quiche/quic/core/frames/quic_connection_close_frame.h",
        "quiche/quic/core/frames/quic_crypto_frame.h",
        "quiche/quic/core/frames/quic_frame.h",
        "quiche/quic/core/frames/quic_goaway_frame.h",
        "quiche/quic/core/frames/quic_handshake_done_frame.h",
        "quiche/quic/core/frames/quic_inlined_frame.h",
        "quiche/quic/core/frames/quic_max_streams_frame.h",
        "quiche/quic/core/frames/quic_message_frame.h",
        "quiche/quic/core/frames/quic_mtu_discovery_frame.h",
        "quiche/quic/core/frames/quic_new_connection_id_frame.h",
        "quiche/quic/core/frames/quic_new_token_frame.h",
        "quiche/quic/core/frames/quic_padding_frame.h",
        "quiche/quic/core/frames/quic_path_challenge_frame.h",
        "quiche/quic/core/frames/quic_path_response_frame.h",
        "quiche/quic/core/frames/quic_ping_frame.h",
        "quiche/quic/core/frames/quic_reset_stream_at_frame.h",
        "quiche/quic/core/frames/quic_retire_connection_id_frame.h",
        "quiche/quic/core/frames/quic_rst_stream_frame.h",
        "quiche/quic/core/frames/quic_stop_sending_frame.h",
        "quiche/quic/core/frames/quic_stop_waiting_frame.h",
        "quiche/quic/core/frames/quic_stream_frame.h",
        "quiche/quic/core/frames/quic_streams_blocked_frame.h",
        "quiche/quic/core/frames/quic_window_update_frame.h",
    ],
    copts = quiche_copts,
    # TODO: Work around initializer in anonymous union in fastbuild build.
    # Remove this after upstream fix.
    defines = select({
        "@envoy//bazel:windows_x86_64": ["QUIC_FRAME_DEBUG=0"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_interval_lib",
        ":quic_core_interval_set_lib",
        ":quic_core_types_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quiche_common_buffer_allocator_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_http_constants_lib",
    srcs = ["quiche/quic/core/http/http_constants.cc"],
    hdrs = ["quiche/quic/core/http/http_constants.h"],
    deps = [":quic_core_types_lib"],
)

envoy_cc_library(
    name = "quiche_common_capsule_lib",
    srcs = ["quiche/common/capsule.cc"],
    hdrs = ["quiche/common/capsule.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_data_lib",
        ":quic_core_http_http_frames_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
        ":quiche_common_buffer_allocator_lib",
        ":quiche_common_ip_address",
        ":quiche_common_wire_serialization",
        ":quiche_web_transport_web_transport_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_connect_udp_datagram_payload_lib",
    srcs = ["quiche/common/masque/connect_udp_datagram_payload.cc"],
    hdrs = ["quiche/common/masque/connect_udp_datagram_payload.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_lib",
        ":quiche_common_platform_bug_tracker",
        ":quiche_common_platform_logging",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "quiche_common_quiche_stream_lib",
    srcs = [],
    hdrs = ["quiche/common/quiche_stream.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_common_platform_export",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_client_lib",
    srcs = [
        "quiche/quic/core/http/quic_spdy_client_session.cc",
        "quiche/quic/core/http/quic_spdy_client_session_base.cc",
        "quiche/quic/core/http/quic_spdy_client_stream.cc",
    ],
    hdrs = [
        "quiche/quic/core/http/quic_spdy_client_session.h",
        "quiche/quic/core/http/quic_spdy_client_session_base.h",
        "quiche/quic/core/http/quic_spdy_client_stream.h",
    ],
    deps = [
        ":http2_core_framer_lib",
        ":http2_core_protocol_lib",
        ":quic_client_session_lib",
        ":quic_core_alarm_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_http_server_initiated_spdy_stream_lib",
        ":quic_core_http_spdy_session_lib",
        ":quic_core_packets_lib",
        ":quic_core_qpack_qpack_streams_lib",
        ":quic_core_server_id_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_header_list_lib",
    srcs = ["quiche/quic/core/http/quic_header_list.cc"],
    hdrs = ["quiche/quic/core/http/quic_header_list.h"],
    deps = [
        ":common_http_http_header_block_lib",
        ":http2_core_headers_handler_interface_lib",
        ":http2_core_protocol_lib",
        ":quic_core_packets_lib",
        ":quic_core_qpack_qpack_header_table_lib",
        ":quic_platform_base",
        ":quiche_common_circular_deque_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_http_decoder_lib",
    srcs = ["quiche/quic/core/http/http_decoder.cc"],
    hdrs = ["quiche/quic/core/http/http_decoder.h"],
    deps = [
        ":http2_constants_lib",
        ":quic_core_data_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_http_http_frames_lib",
        ":quic_core_http_spdy_utils_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
        "@com_google_absl//absl/base:nullability",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_http_encoder_lib",
    srcs = ["quiche/quic/core/http/http_encoder.cc"],
    hdrs = ["quiche/quic/core/http/http_encoder.h"],
    deps = [
        ":quic_core_data_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_http_http_frames_lib",
        ":quic_core_http_spdy_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_http_frames_lib",
    hdrs = ["quiche/quic/core/http/http_frames.h"],
    deps = [
        ":http2_core_framer_lib",
        ":quic_core_http_http_constants_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_metadata_decoder_lib",
    srcs = ["quiche/quic/core/http/metadata_decoder.cc"],
    hdrs = ["quiche/quic/core/http/metadata_decoder.h"],
    deps = [
        ":quic_core_error_codes_lib",
        ":quic_core_http_header_list_lib",
        ":quic_core_qpack_qpack_decoded_headers_accumulator_lib",
        ":quic_core_qpack_qpack_decoder_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_server_initiated_spdy_stream_lib",
    srcs = ["quiche/quic/core/http/quic_server_initiated_spdy_stream.cc"],
    hdrs = ["quiche/quic/core/http/quic_server_initiated_spdy_stream.h"],
    deps = [
        ":quic_core_http_spdy_session_lib",
        ":quic_core_types_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_spdy_session_lib",
    srcs = [
        "quiche/quic/core/http/quic_headers_stream.cc",
        "quiche/quic/core/http/quic_receive_control_stream.cc",
        "quiche/quic/core/http/quic_send_control_stream.cc",
        "quiche/quic/core/http/quic_spdy_session.cc",
        "quiche/quic/core/http/quic_spdy_stream.cc",
        "quiche/quic/core/http/web_transport_http3.cc",
        "quiche/quic/core/http/web_transport_stream_adapter.cc",
        "quiche/quic/core/web_transport_stats.cc",
    ],
    hdrs = [
        "quiche/quic/core/http/quic_headers_stream.h",
        "quiche/quic/core/http/quic_receive_control_stream.h",
        "quiche/quic/core/http/quic_send_control_stream.h",
        "quiche/quic/core/http/quic_spdy_session.h",
        "quiche/quic/core/http/quic_spdy_stream.h",
        "quiche/quic/core/http/web_transport_http3.h",
        "quiche/quic/core/http/web_transport_stream_adapter.h",
        "quiche/quic/core/web_transport_stats.h",
    ],
    deps = [
        ":http2_adapter_header_validator",
        ":http2_core_framer_lib",
        ":http2_core_http2_deframer_lib",
        ":http2_core_protocol_lib",
        ":quic_core_connection_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_http_header_list_lib",
        ":quic_core_http_http_constants_lib",
        ":quic_core_http_http_decoder_lib",
        ":quic_core_http_http_encoder_lib",
        ":quic_core_http_metadata_decoder_lib",
        ":quic_core_http_spdy_stream_body_manager_lib",
        ":quic_core_http_spdy_utils_lib",
        ":quic_core_packets_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_qpack_qpack_decoded_headers_accumulator_lib",
        ":quic_core_qpack_qpack_decoder_lib",
        ":quic_core_qpack_qpack_decoder_stream_sender_lib",
        ":quic_core_qpack_qpack_encoder_lib",
        ":quic_core_qpack_qpack_encoder_stream_sender_lib",
        ":quic_core_qpack_qpack_streams_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_core_web_transport_interface_lib",
        ":quic_platform_base",
        ":quiche_common_capsule_lib",
        ":quiche_common_mem_slice_storage",
        ":quiche_common_structured_headers_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_server_http_spdy_session_lib",
    srcs = [
        "quiche/quic/core/http/quic_server_session_base.cc",
        "quiche/quic/core/http/quic_spdy_server_stream_base.cc",
    ],
    hdrs = [
        "quiche/quic/core/http/quic_server_session_base.h",
        "quiche/quic/core/http/quic_spdy_server_stream_base.h",
    ],
    deps = [
        ":quic_core_http_spdy_session_lib",
        ":quic_server_session_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_spdy_stream_body_manager_lib",
    srcs = ["quiche/quic/core/http/quic_spdy_stream_body_manager.cc"],
    hdrs = ["quiche/quic/core/http/quic_spdy_stream_body_manager.h"],
    deps = [
        ":quic_core_http_http_decoder_lib",
        ":quic_core_session_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_http_spdy_utils_lib",
    srcs = ["quiche/quic/core/http/spdy_utils.cc"],
    hdrs = ["quiche/quic/core/http/spdy_utils.h"],
    deps = [
        ":http2_core_framer_lib",
        ":http2_core_protocol_lib",
        ":quic_core_http_header_list_lib",
        ":quic_core_http_http_constants_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_idle_network_detector_lib",
    srcs = ["quiche/quic/core/quic_idle_network_detector.cc"],
    hdrs = ["quiche/quic/core/quic_idle_network_detector.h"],
    deps = [
        ":quic_core_alarm_factory_lib",
        ":quic_core_alarm_lib",
        ":quic_core_constants_lib",
        ":quic_core_one_block_arena_lib",
        ":quic_core_time_lib",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_interval_lib",
    hdrs = ["quiche/quic/core/quic_interval.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_interval_deque_lib",
    hdrs = ["quiche/quic/core/quic_interval_deque.h"],
    deps = [
        ":quic_core_interval_lib",
        ":quic_core_types_lib",
        ":quic_platform",
        ":quiche_common_circular_deque_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_interval_set_lib",
    hdrs = ["quiche/quic/core/quic_interval_set.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_interval_lib",
        ":quic_platform_base",
        ":quiche_common_platform_containers",
    ],
)

envoy_cc_library(
    name = "quic_core_io_event_loop",
    hdrs = select({
        "@envoy//bazel:windows_x86_64": [],
        "@envoy//bazel:disable_http3": [],
        "//conditions:default": ["quiche/quic/core/io/quic_event_loop.h"],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = select({
        "@envoy//bazel:windows_x86_64": [],
        "@envoy//bazel:disable_http3": [],
        "//conditions:default": [
            ":quic_core_alarm_factory_lib",
            ":quic_core_clock_lib",
            ":quic_core_udp_socket_lib",
            "@com_google_absl//absl/base:core_headers",
        ],
    }),
)

envoy_cc_library(
    name = "quic_core_io_socket_lib",
    srcs = ["quiche/quic/core/io/socket.cc"],
    hdrs = [
        "quiche/quic/core/connecting_client_socket.h",
        "quiche/quic/core/io/socket.h",
        "quiche/quic/core/io/socket_internal.h",
        "quiche/quic/core/io/socket_posix.inc",
        "quiche/quic/core/io/socket_win.inc",
        "quiche/quic/core/socket_factory.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_ip_address_family",
        ":quic_platform_socket_address",
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

envoy_cc_library(
    name = "quic_core_io_event_loop_socket_factory_lib",
    srcs = select({
        "@envoy//bazel:windows_x86_64": [],
        "@envoy//bazel:disable_http3": [],
        "//conditions:default": [
            "quiche/quic/core/io/event_loop_connecting_client_socket.cc",
            "quiche/quic/core/io/event_loop_socket_factory.cc",
        ],
    }),
    hdrs = select({
        "@envoy//bazel:windows_x86_64": [],
        "@envoy//bazel:disable_http3": [],
        "//conditions:default": [
            "quiche/quic/core/io/event_loop_connecting_client_socket.h",
            "quiche/quic/core/io/event_loop_socket_factory.h",
        ],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = select({
        "@envoy//bazel:windows_x86_64": [],
        "@envoy//bazel:disable_http3": [],
        "//conditions:default": [
            ":quic_core_io_event_loop",
            ":quic_core_io_socket_lib",
            ":quic_core_types_lib",
            ":quic_platform_socket_address",
            ":quiche_common_buffer_allocator_lib",
            ":quiche_common_platform",
            "@com_google_absl//absl/status:statusor",
            "@com_google_absl//absl/strings",
            "@com_google_absl//absl/types:optional",
            "@com_google_absl//absl/types:span",
            "@com_google_absl//absl/types:variant",
        ],
    }),
)

envoy_cc_library(
    name = "quic_core_lru_cache_lib",
    hdrs = ["quiche/quic/core/quic_lru_cache.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [":quic_platform_base"],
)

envoy_quic_cc_library(
    name = "quic_core_mtu_discovery_lib",
    srcs = ["quiche/quic/core/quic_mtu_discovery.cc"],
    hdrs = ["quiche/quic/core/quic_mtu_discovery.h"],
    deps = [
        ":quic_core_constants_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_one_block_arena_lib",
    srcs = ["quiche/quic/core/quic_one_block_arena.h"],
    deps = [
        ":quic_core_arena_scoped_ptr_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_syscall_wrapper_lib",
    srcs = select({
        "@envoy//bazel:linux": ["quiche/quic/core/quic_syscall_wrapper.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:linux": ["quiche/quic/core/quic_syscall_wrapper.h"],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_linux_socket_utils_lib",
    srcs = select({
        "@envoy//bazel:linux": ["quiche/quic/core/quic_linux_socket_utils.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:linux": ["quiche/quic/core/quic_linux_socket_utils.h"],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_packet_writer_lib",
        ":quic_core_syscall_wrapper_lib",
        ":quic_core_types_lib",
        ":quic_platform",
        ":quiche_common_callbacks",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_network_blackhole_detector_lib",
    srcs = ["quiche/quic/core/quic_network_blackhole_detector.cc"],
    hdrs = ["quiche/quic/core/quic_network_blackhole_detector.h"],
    deps = [
        ":quic_core_alarm_factory_lib",
        ":quic_core_alarm_lib",
        ":quic_core_constants_lib",
        ":quic_core_one_block_arena_lib",
        ":quic_core_time_lib",
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_packet_creator_lib",
    srcs = ["quiche/quic/core/quic_packet_creator.cc"],
    hdrs = ["quiche/quic/core/quic_packet_creator.h"],
    deps = [
        ":quic_core_chaos_protector_lib",
        ":quic_core_coalesced_packet_lib",
        ":quic_core_constants_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_data_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quiche_common_circular_deque_lib",
        ":quiche_common_print_elements_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_packet_number_indexed_queue_lib",
    hdrs = ["quiche/quic/core/packet_number_indexed_queue.h"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
        ":quiche_common_circular_deque_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_packet_writer_lib",
    srcs = ["quiche/quic/core/quic_packet_writer_wrapper.cc"],
    hdrs = [
        "quiche/quic/core/quic_packet_writer.h",
        "quiche/quic/core/quic_packet_writer_wrapper.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_packets_lib",
    srcs = [
        "quiche/quic/core/quic_packets.cc",
        "quiche/quic/core/quic_write_blocked_list.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_packets.h",
        "quiche/quic/core/quic_write_blocked_list.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":http2_core_priority_write_scheduler_lib",
        ":quic_core_ack_listener_interface_lib",
        ":quic_core_bandwidth_lib",
        ":quic_core_constants_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform",
        ":quic_platform_socket_address",
        ":quic_stream_priority_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_path_validator_lib",
    srcs = ["quiche/quic/core/quic_path_validator.cc"],
    hdrs = ["quiche/quic/core/quic_path_validator.h"],
    deps = [
        ":quic_core_alarm_factory_lib",
        ":quic_core_alarm_lib",
        ":quic_core_arena_scoped_ptr_lib",
        ":quic_core_clock_lib",
        ":quic_core_constants_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_one_block_arena_lib",
        ":quic_core_packet_writer_lib",
        ":quic_core_types_lib",
        ":quic_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_ping_manager_lib",
    srcs = ["quiche/quic/core/quic_ping_manager.cc"],
    hdrs = ["quiche/quic/core/quic_ping_manager.h"],
    deps = [
        ":quic_core_alarm_factory_lib",
        ":quic_core_alarm_lib",
        ":quic_core_constants_lib",
        ":quic_core_one_block_arena_lib",
        ":quic_core_time_lib",
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_process_packet_interface_lib",
    hdrs = ["quiche/quic/core/quic_process_packet_interface.h"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_blocking_manager_lib",
    srcs = ["quiche/quic/core/qpack/qpack_blocking_manager.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_blocking_manager.h"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_decoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoder.h"],
    deps = [
        ":quic_core_qpack_qpack_decoder_stream_sender_lib",
        ":quic_core_qpack_qpack_encoder_stream_receiver_lib",
        ":quic_core_qpack_qpack_header_table_lib",
        ":quic_core_qpack_qpack_progressive_decoder_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_encoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_encoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_encoder.h"],
    deps = [
        ":quic_core_qpack_blocking_manager_lib",
        ":quic_core_qpack_qpack_decoder_stream_receiver_lib",
        ":quic_core_qpack_qpack_encoder_stream_sender_lib",
        ":quic_core_qpack_qpack_header_table_lib",
        ":quic_core_qpack_qpack_index_conversions_lib",
        ":quic_core_qpack_qpack_instruction_encoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_core_qpack_qpack_required_insert_count_lib",
        ":quic_core_qpack_value_splitting_header_list_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_header_table_lib",
    srcs = ["quiche/quic/core/qpack/qpack_header_table.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_header_table.h"],
    deps = [
        ":http2_hpack_hpack_lib",
        ":quic_core_qpack_qpack_static_table_lib",
        ":quic_platform_base",
        ":quiche_common_circular_deque_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_instruction_decoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_instruction_decoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_instruction_decoder.h"],
    deps = [
        ":http2_hpack_huffman_hpack_huffman_decoder_lib",
        ":http2_hpack_varint_hpack_varint_decoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_instructions_lib",
    srcs = ["quiche/quic/core/qpack/qpack_instructions.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_instructions.h"],
    deps = [":quic_platform_base"],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_instruction_encoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_instruction_encoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_instruction_encoder.h"],
    deps = [
        ":http2_hpack_huffman_hpack_huffman_encoder_lib",
        ":http2_hpack_varint_hpack_varint_encoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_progressive_decoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_progressive_decoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_progressive_decoder.h"],
    deps = [
        ":quic_core_qpack_qpack_decoder_stream_sender_lib",
        ":quic_core_qpack_qpack_encoder_stream_receiver_lib",
        ":quic_core_qpack_qpack_header_table_lib",
        ":quic_core_qpack_qpack_index_conversions_lib",
        ":quic_core_qpack_qpack_instruction_decoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_core_qpack_qpack_required_insert_count_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_required_insert_count_lib",
    srcs = ["quiche/quic/core/qpack/qpack_required_insert_count.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_required_insert_count.h"],
    deps = [":quic_platform_base"],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_encoder_stream_sender_lib",
    srcs = ["quiche/quic/core/qpack/qpack_encoder_stream_sender.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_encoder_stream_sender.h"],
    deps = [
        ":quic_core_qpack_qpack_instruction_encoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_core_qpack_qpack_stream_sender_delegate_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_encoder_stream_receiver_lib",
    srcs = ["quiche/quic/core/qpack/qpack_encoder_stream_receiver.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_encoder_stream_receiver.h"],
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_qpack_qpack_instruction_decoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_core_qpack_qpack_stream_receiver_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_decoder_stream_sender_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoder_stream_sender.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoder_stream_sender.h"],
    deps = [
        ":quic_core_qpack_qpack_instruction_encoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_core_qpack_qpack_stream_sender_delegate_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_decoder_stream_receiver_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoder_stream_receiver.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoder_stream_receiver.h"],
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":quic_core_qpack_qpack_instruction_decoder_lib",
        ":quic_core_qpack_qpack_instructions_lib",
        ":quic_core_qpack_qpack_stream_receiver_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_index_conversions_lib",
    srcs = ["quiche/quic/core/qpack/qpack_index_conversions.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_index_conversions.h"],
    deps = [
        ":quic_platform_base",
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_static_table_lib",
    srcs = ["quiche/quic/core/qpack/qpack_static_table.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_static_table.h"],
    deps = [
        ":http2_hpack_hpack_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_stream_receiver_lib",
    hdrs = ["quiche/quic/core/qpack/qpack_stream_receiver.h"],
    deps = [":quic_platform_base"],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_streams_lib",
    srcs = [
        "quiche/quic/core/qpack/qpack_receive_stream.cc",
        "quiche/quic/core/qpack/qpack_send_stream.cc",
    ],
    hdrs = [
        "quiche/quic/core/qpack/qpack_receive_stream.h",
        "quiche/quic/core/qpack/qpack_send_stream.h",
    ],
    deps = [
        ":quic_core_qpack_qpack_stream_receiver_lib",
        ":quic_core_qpack_qpack_stream_sender_delegate_lib",
        ":quic_core_session_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_decoded_headers_accumulator_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoded_headers_accumulator.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoded_headers_accumulator.h"],
    deps = [
        ":quic_core_http_header_list_lib",
        ":quic_core_qpack_qpack_decoder_lib",
        ":quic_core_qpack_qpack_progressive_decoder_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_value_splitting_header_list_lib",
    srcs = ["quiche/quic/core/qpack/value_splitting_header_list.cc"],
    hdrs = ["quiche/quic/core/qpack/value_splitting_header_list.h"],
    deps = [
        ":common_http_http_header_block_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_qpack_qpack_stream_sender_delegate_lib",
    hdrs = ["quiche/quic/core/qpack/qpack_stream_sender_delegate.h"],
    deps = [":quic_platform_base"],
)

envoy_quic_cc_library(
    name = "quic_core_received_packet_manager_lib",
    srcs = ["quiche/quic/core/quic_received_packet_manager.cc"],
    hdrs = ["quiche/quic/core/quic_received_packet_manager.h"],
    deps = [
        ":quic_core_config_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_sent_packet_manager_lib",
    srcs = ["quiche/quic/core/quic_sent_packet_manager.cc"],
    hdrs = ["quiche/quic/core/quic_sent_packet_manager.h"],
    deps = [
        ":quic_core_congestion_control_congestion_control_lib",
        ":quic_core_congestion_control_general_loss_algorithm_lib",
        ":quic_core_congestion_control_pacing_sender_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_congestion_control_uber_loss_algorithm_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_packets_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_sustained_bandwidth_recorder_lib",
        ":quic_core_transmission_info_lib",
        ":quic_core_types_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quiche_common_print_elements_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_web_transport_interface_lib",
    hdrs = ["quiche/quic/core/web_transport_interface.h"],
    deps = [
        ":quic_core_session_lib",
        ":quic_core_types_lib",
        ":quic_platform_export",
        ":quiche_web_transport_web_transport_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_server_id_lib",
    srcs = ["quiche/quic/core/quic_server_id.cc"],
    hdrs = ["quiche/quic/core/quic_server_id.h"],
    deps = [
        ":quic_platform_base",
        ":quiche_common_platform_googleurl",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_server_lib",
    srcs = [
        "quiche/quic/core/chlo_extractor.cc",
        "quiche/quic/core/quic_buffered_packet_store.cc",
        "quiche/quic/core/quic_dispatcher.cc",
        "quiche/quic/core/quic_dispatcher_stats.cc",
        "quiche/quic/core/tls_chlo_extractor.cc",
    ],
    hdrs = [
        "quiche/quic/core/chlo_extractor.h",
        "quiche/quic/core/quic_buffered_packet_store.h",
        "quiche/quic/core/quic_dispatcher.h",
        "quiche/quic/core/quic_dispatcher_stats.h",
        "quiche/quic/core/tls_chlo_extractor.h",
    ],
    deps = [
        ":quic_core_alarm_factory_lib",
        ":quic_core_alarm_lib",
        ":quic_core_blocked_writer_interface_lib",
        ":quic_core_blocked_writer_list_lib",
        ":quic_core_connection_id_generator_interface_lib",
        ":quic_core_connection_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_deterministic_connection_id_generator_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_core_process_packet_interface_lib",
        ":quic_core_time_lib",
        ":quic_core_time_wait_list_manager_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_version_manager_lib",
        ":quic_platform",
        ":quic_server_session_lib",
        ":quiche_common_callbacks",
        ":quiche_common_intrusive_list_lib",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_cc_library(
    name = "quic_stream_priority_lib",
    srcs = [
        "quiche/quic/core/quic_stream_priority.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_stream_priority.h",
    ],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_export",
        ":quiche_common_platform_bug_tracker",
        ":quiche_common_structured_headers_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_session_lib",
    srcs = [
        "quiche/quic/core/legacy_quic_stream_id_manager.cc",
        "quiche/quic/core/quic_control_frame_manager.cc",
        "quiche/quic/core/quic_crypto_handshaker.cc",
        "quiche/quic/core/quic_crypto_stream.cc",
        "quiche/quic/core/quic_datagram_queue.cc",
        "quiche/quic/core/quic_flow_controller.cc",
        "quiche/quic/core/quic_session.cc",
        "quiche/quic/core/quic_stream.cc",
        "quiche/quic/core/quic_stream_id_manager.cc",
        "quiche/quic/core/quic_stream_sequencer.cc",
        "quiche/quic/core/tls_handshaker.cc",
        "quiche/quic/core/uber_quic_stream_id_manager.cc",
        "quiche/quic/core/web_transport_write_blocked_list.cc",
    ],
    hdrs = [
        "quiche/common/btree_scheduler.h",
        "quiche/quic/core/handshaker_delegate_interface.h",
        "quiche/quic/core/legacy_quic_stream_id_manager.h",
        "quiche/quic/core/quic_control_frame_manager.h",
        "quiche/quic/core/quic_crypto_client_stream.h",  # required by tls_client_handshaker.h
        "quiche/quic/core/quic_crypto_handshaker.h",
        "quiche/quic/core/quic_crypto_stream.h",
        "quiche/quic/core/quic_datagram_queue.h",
        "quiche/quic/core/quic_flow_controller.h",
        "quiche/quic/core/quic_session.h",
        "quiche/quic/core/quic_stream.h",
        "quiche/quic/core/quic_stream_id_manager.h",
        "quiche/quic/core/quic_stream_sequencer.h",
        "quiche/quic/core/stream_delegate_interface.h",
        "quiche/quic/core/tls_client_handshaker.h",  # required by tls_handshaker.cc
        "quiche/quic/core/tls_handshaker.h",
        "quiche/quic/core/uber_quic_stream_id_manager.h",
        "quiche/quic/core/web_transport_write_blocked_list.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":http2_core_protocol_lib",
        ":quic_client_crypto_crypto_handshake_lib",
        ":quic_core_config_lib",
        ":quic_core_connection_lib",
        ":quic_core_constants_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_crypto_tls_handshake_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_http_http_decoder_lib",
        ":quic_core_http_http_encoder_lib",
        ":quic_core_packet_creator_lib",
        ":quic_core_packets_lib",
        ":quic_core_server_id_lib",
        ":quic_core_session_notifier_interface_lib",
        ":quic_core_stream_frame_data_producer_lib",
        ":quic_core_stream_send_buffer_lib",
        ":quic_core_stream_sequencer_buffer_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform",
        ":quic_stream_priority_lib",
        ":quiche_common_callbacks",
        ":quiche_common_structured_headers_lib",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_client_session_lib",
    srcs = [
        "quiche/quic/core/quic_crypto_client_handshaker.cc",
        "quiche/quic/core/quic_crypto_client_stream.cc",
        "quiche/quic/core/tls_client_handshaker.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_crypto_client_handshaker.h",
        "quiche/quic/core/quic_crypto_client_stream.h",
        "quiche/quic/core/tls_client_handshaker.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":quic_client_crypto_crypto_handshake_lib",
        ":quic_core_session_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_server_session_lib",
    srcs = [
        "quiche/quic/core/quic_crypto_server_stream.cc",
        "quiche/quic/core/quic_crypto_server_stream_base.cc",
        "quiche/quic/core/tls_server_handshaker.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_crypto_server_stream.h",
        "quiche/quic/core/quic_crypto_server_stream_base.h",
        "quiche/quic/core/tls_server_handshaker.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":quic_core_session_lib",
        ":quic_server_crypto_crypto_handshake_lib",
        ":quic_server_crypto_tls_handshake_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_session_notifier_interface_lib",
    hdrs = ["quiche/quic/core/session_notifier_interface.h"],
    deps = [
        ":quic_core_frames_frames_lib",
        ":quic_core_time_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_socket_address_coder_lib",
    srcs = ["quiche/quic/core/quic_socket_address_coder.cc"],
    hdrs = ["quiche/quic/core/quic_socket_address_coder.h"],
    deps = [
        ":quic_platform_base",
        ":quic_platform_socket_address",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_stream_frame_data_producer_lib",
    hdrs = ["quiche/quic/core/quic_stream_frame_data_producer.h"],
    deps = [":quic_core_types_lib"],
)

envoy_quic_cc_library(
    name = "quic_core_stream_send_buffer_lib",
    srcs = ["quiche/quic/core/quic_stream_send_buffer.cc"],
    hdrs = ["quiche/quic/core/quic_stream_send_buffer.h"],
    deps = [
        ":quic_core_data_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_interval_deque_lib",
        ":quic_core_interval_lib",
        ":quic_core_interval_set_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quiche_common_circular_deque_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_stream_sequencer_buffer_lib",
    srcs = ["quiche/quic/core/quic_stream_sequencer_buffer.cc"],
    hdrs = ["quiche/quic/core/quic_stream_sequencer_buffer.h"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_interval_lib",
        ":quic_core_interval_set_lib",
        ":quic_core_packets_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_sustained_bandwidth_recorder_lib",
    srcs = ["quiche/quic/core/quic_sustained_bandwidth_recorder.cc"],
    hdrs = ["quiche/quic/core/quic_sustained_bandwidth_recorder.h"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_tag_lib",
    srcs = ["quiche/quic/core/quic_tag.cc"],
    hdrs = ["quiche/quic/core/quic_tag.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_time_lib",
    srcs = ["quiche/quic/core/quic_time.cc"],
    hdrs = ["quiche/quic/core/quic_time.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quic_platform_base"],
)

envoy_quic_cc_library(
    name = "quic_core_time_accumulator_lib",
    hdrs = ["quiche/quic/core/quic_time_accumulator.h"],
    deps = [],
)

envoy_quic_cc_library(
    name = "quic_core_time_wait_list_manager_lib",
    srcs = ["quiche/quic/core/quic_time_wait_list_manager.cc"],
    hdrs = ["quiche/quic/core/quic_time_wait_list_manager.h"],
    deps = [
        ":quic_core_blocked_writer_interface_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_packet_writer_lib",
        ":quic_core_packets_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform",
        ":quic_server_session_lib",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_transmission_info_lib",
    srcs = ["quiche/quic/core/quic_transmission_info.cc"],
    hdrs = ["quiche/quic/core/quic_transmission_info.h"],
    deps = [
        ":quic_core_ack_listener_interface_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_types_lib",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_types_lib",
    srcs = [
        "quiche/quic/core/quic_connection_id.cc",
        "quiche/quic/core/quic_packet_number.cc",
        "quiche/quic/core/quic_types.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_connection_id.h",
        "quiche/quic/core/quic_packet_number.h",
        "quiche/quic/core/quic_types.h",
    ],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_crypto_random_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
        ":quiche_common_endian_lib",
        ":quiche_common_print_elements_lib",
        ":quiche_web_transport_web_transport_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_uber_received_packet_manager_lib",
    srcs = ["quiche/quic/core/uber_received_packet_manager.cc"],
    hdrs = ["quiche/quic/core/uber_received_packet_manager.h"],
    deps = [
        ":quic_core_received_packet_manager_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_udp_socket_lib",
    srcs = select({
        "@envoy//bazel:windows_x86_64": [],
        "//conditions:default": ["quiche/quic/core/quic_udp_socket.cc"],
    }),
    hdrs = select({
        "@envoy//bazel:windows_x86_64": [],
        "//conditions:default": [
            "quiche/quic/core/quic_udp_socket.h",
            "quiche/quic/core/quic_udp_socket_posix.inc",
        ],
    }),
    copts = quiche_copts + select({
        # On OSX/iOS, constants from RFC 3542 (e.g. IPV6_RECVPKTINFO) are not usable
        # without this define.
        "@envoy//bazel:apple": ["-D__APPLE_USE_RFC_3542"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_io_socket_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform",
        ":quic_platform_udp_socket_platform",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_unacked_packet_map_lib",
    srcs = ["quiche/quic/core/quic_unacked_packet_map.cc"],
    hdrs = ["quiche/quic/core/quic_unacked_packet_map.h"],
    deps = [
        ":quic_core_connection_stats_lib",
        ":quic_core_packets_lib",
        ":quic_core_session_notifier_interface_lib",
        ":quic_core_transmission_info_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quiche_common_circular_deque_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_utils_lib",
    srcs = ["quiche/quic/core/quic_utils.cc"],
    hdrs = ["quiche/quic/core/quic_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_types_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quic_platform_socket_address",
        ":quiche_common_buffer_allocator_lib",
    ],
)

envoy_quic_cc_library(
    name = "quic_core_version_manager_lib",
    srcs = ["quiche/quic/core/quic_version_manager.cc"],
    hdrs = ["quiche/quic/core/quic_version_manager.h"],
    deps = [
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quiche_common_endian_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_versions_lib",
    srcs = ["quiche/quic/core/quic_versions.cc"],
    hdrs = ["quiche/quic/core/quic_versions.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_crypto_random_lib",
        ":quic_core_tag_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
        ":quiche_common_text_utils_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_config_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_config_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_config_peer.h"],
    deps = [
        ":quic_core_config_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_connection_id_manager_peer_lib",
    hdrs = ["quiche/quic/test_tools/quic_connection_id_manager_peer.h"],
    deps = [":quic_core_connection_id_manager"],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_crypto_server_config_peer_lib",
    srcs = [
        "quiche/quic/test_tools/quic_crypto_server_config_peer.cc",
    ],
    hdrs = [
        "quiche/quic/test_tools/quic_crypto_server_config_peer.h",
    ],
    deps = [
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_test_tools_mock_clock_lib",
        ":quic_test_tools_mock_random_lib",
        ":quic_test_tools_test_utils_lib",
        ":quiche_common_platform",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_first_flight_lib",
    srcs = [
        "quiche/quic/test_tools/first_flight.cc",
    ],
    hdrs = [
        "quiche/quic/test_tools/first_flight.h",
    ],
    deps = [
        ":quic_core_config_lib",
        ":quic_core_connection_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_http_client_lib",
        ":quic_core_packet_writer_lib",
        ":quic_core_packets_lib",
        ":quic_core_types_lib",
        ":quic_core_versions_lib",
        ":quic_platform",
        ":quic_test_tools_test_utils_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_flow_controller_peer_lib",
    srcs = [
        "quiche/quic/test_tools/quic_flow_controller_peer.cc",
    ],
    hdrs = [
        "quiche/quic/test_tools/quic_flow_controller_peer.h",
    ],
    deps = [
        ":quic_core_packets_lib",
        ":quic_core_session_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_framer_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_framer_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_framer_peer.h"],
    deps = [
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_interval_deque_peer_lib",
    hdrs = ["quiche/quic/test_tools/quic_interval_deque_peer.h"],
    deps = [
        ":quic_core_interval_deque_lib",
        ":quic_core_interval_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_mock_clock_lib",
    srcs = ["quiche/quic/test_tools/mock_clock.cc"],
    hdrs = ["quiche/quic/test_tools/mock_clock.h"],
    deps = [
        ":quic_core_clock_lib",
        ":quic_core_time_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_mock_random_lib",
    srcs = ["quiche/quic/test_tools/mock_random.cc"],
    hdrs = ["quiche/quic/test_tools/mock_random.h"],
    deps = [
        ":quic_core_crypto_random_lib",
        ":quic_platform_test",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_mock_syscall_wrapper_lib",
    srcs = ["quiche/quic/test_tools/quic_mock_syscall_wrapper.cc"],
    hdrs = ["quiche/quic/test_tools/quic_mock_syscall_wrapper.h"],
    deps = [
        ":quic_core_syscall_wrapper_lib",
        ":quic_platform_base",
        ":quic_platform_test",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_qpack_qpack_test_utils_lib",
    srcs = ["quiche/quic/test_tools/qpack/qpack_test_utils.cc"],
    hdrs = ["quiche/quic/test_tools/qpack/qpack_test_utils.h"],
    deps = [
        ":quic_core_qpack_qpack_encoder_lib",
        ":quic_core_qpack_qpack_stream_sender_delegate_lib",
        ":quic_platform_test",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_sent_packet_manager_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_sent_packet_manager_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_sent_packet_manager_peer.h"],
    deps = [
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_packets_lib",
        ":quic_core_sent_packet_manager_lib",
        ":quic_test_tools_unacked_packet_map_peer_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_server_session_base_peer",
    hdrs = [
        "quiche/quic/test_tools/quic_server_session_base_peer.h",
    ],
    deps = [
        ":quic_core_utils_lib",
        ":quic_server_http_spdy_session_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_simple_quic_framer_lib",
    srcs = ["quiche/quic/test_tools/simple_quic_framer.cc"],
    hdrs = ["quiche/quic/test_tools/simple_quic_framer.h"],
    deps = [
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_stream_send_buffer_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_stream_send_buffer_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_stream_send_buffer_peer.h"],
    deps = [
        ":quic_core_stream_send_buffer_lib",
        ":quic_test_tools_interval_deque_peer_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_stream_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_stream_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_stream_peer.h"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_core_session_lib",
        ":quic_core_stream_send_buffer_lib",
        ":quic_platform_base",
        ":quic_test_tools_flow_controller_peer_lib",
        ":quic_test_tools_stream_send_buffer_peer_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_test_certificates_lib",
    srcs = ["quiche/quic/test_tools/test_certificates.cc"],
    hdrs = ["quiche/quic/test_tools/test_certificates.h"],
    deps = [
        ":quic_platform_base",
        ":quiche_common_platform",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_test_utils_lib",
    srcs = [
        "quiche/quic/test_tools/crypto_test_utils.cc",
        "quiche/quic/test_tools/mock_quic_session_visitor.cc",
        "quiche/quic/test_tools/mock_quic_time_wait_list_manager.cc",
        "quiche/quic/test_tools/quic_buffered_packet_store_peer.cc",
        "quiche/quic/test_tools/quic_connection_peer.cc",
        "quiche/quic/test_tools/quic_dispatcher_peer.cc",
        "quiche/quic/test_tools/quic_test_utils.cc",
    ],
    hdrs = [
        "quiche/quic/test_tools/crypto_test_utils.h",
        "quiche/quic/test_tools/mock_connection_id_generator.h",
        "quiche/quic/test_tools/mock_quic_session_visitor.h",
        "quiche/quic/test_tools/mock_quic_time_wait_list_manager.h",
        "quiche/quic/test_tools/quic_buffered_packet_store_peer.h",
        "quiche/quic/test_tools/quic_connection_peer.h",
        "quiche/quic/test_tools/quic_dispatcher_peer.h",
        "quiche/quic/test_tools/quic_test_utils.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":http2_core_framer_lib",
        ":quic_client_session_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_connection_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_proof_source_lib",
        ":quic_core_crypto_proof_source_x509_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_data_lib",
        ":quic_core_framer_lib",
        ":quic_core_http_client_lib",
        ":quic_core_packet_creator_lib",
        ":quic_core_packet_writer_lib",
        ":quic_core_packets_lib",
        ":quic_core_path_validator_lib",
        ":quic_core_received_packet_manager_lib",
        ":quic_core_sent_packet_manager_lib",
        ":quic_core_server_id_lib",
        ":quic_core_server_lib",
        ":quic_core_time_wait_list_manager_lib",
        ":quic_core_utils_lib",
        ":quic_platform",
        ":quic_platform_hostname_utils",
        ":quic_platform_test",
        ":quic_server_http_spdy_session_lib",
        ":quic_server_session_lib",
        ":quic_test_tools_config_peer_lib",
        ":quic_test_tools_connection_id_manager_peer_lib",
        ":quic_test_tools_framer_peer_lib",
        ":quic_test_tools_mock_clock_lib",
        ":quic_test_tools_mock_random_lib",
        ":quic_test_tools_sent_packet_manager_peer_lib",
        ":quic_test_tools_simple_quic_framer_lib",
        ":quic_test_tools_stream_peer_lib",
        ":quic_test_tools_test_certificates_lib",
        ":quiche_common_buffer_allocator_lib",
        ":quiche_common_callbacks",
        ":quiche_common_test_tools_test_utils_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_session_peer_lib",
    srcs = [
        "quiche/quic/test_tools/quic_session_peer.cc",
    ],
    hdrs = [
        "quiche/quic/test_tools/quic_session_peer.h",
    ],
    deps = [
        ":quic_client_session_lib",
        ":quic_core_packets_lib",
        ":quic_core_utils_lib",
        ":quic_platform",
        ":quic_server_session_lib",
    ],
)

envoy_quic_cc_test_library(
    name = "quic_test_tools_unacked_packet_map_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_unacked_packet_map_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_unacked_packet_map_peer.h"],
    deps = [":quic_core_unacked_packet_map_lib"],
)

envoy_cc_library(
    name = "quiche_common_endian_lib",
    hdrs = ["quiche/common/quiche_endian.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps =
        [
            ":quiche_common_platform_export",
        ],
)

envoy_cc_library(
    name = "quiche_common_platform_client_stats",
    hdrs = [
        "quiche/common/platform/api/quiche_client_stats.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quiche_common_platform_default_quiche_platform_impl_client_stats_impl_lib"],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_client_stats_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_client_stats_impl.h",
    ],
)

envoy_cc_test_library(
    name = "quiche_common_platform_system_event_loop",
    hdrs = [
        "quiche/common/platform/api/quiche_system_event_loop.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_default_quiche_platform_impl_system_event_loop_impl_lib"],
)

envoy_quiche_platform_impl_cc_test_library(
    name = "quiche_common_platform_default_quiche_platform_impl_system_event_loop_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_system_event_loop_impl.h",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_googleurl",
    hdrs = ["quiche/common/platform/api/quiche_googleurl.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_default_quiche_platform_impl_googleurl_impl_lib"],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_googleurl_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_googleurl_impl.h",
    ],
    deps = [
        "@com_googlesource_googleurl//url",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_iovec",
    hdrs = [
        "quiche/common/platform/api/quiche_iovec.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_bug_tracker",
        ":quiche_common_platform_export",
        "@envoy//source/common/quic/platform:quiche_platform_iovec_impl_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_bug_tracker",
    hdrs = [
        "quiche/common/platform/api/quiche_bug_tracker.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
    ] + select({
        "@platforms//os:android": [
            "@envoy//source/common/quic/platform/mobile_impl:mobile_quiche_bug_tracker_impl_lib",
        ],
        "@platforms//os:ios": [
            "@envoy//source/common/quic/platform/mobile_impl:mobile_quiche_bug_tracker_impl_lib",
        ],
        "//conditions:default": ["@envoy//source/common/quic/platform:quiche_logging_impl_lib"],
    }),
)

envoy_cc_library(
    name = "quiche_common_platform_logging",
    hdrs = [
        "quiche/common/platform/api/quiche_logging.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
    ] + select({
        "@platforms//os:android": [
            ":quiche_common_mobile_quiche_logging_lib",
        ],
        "@platforms//os:ios": [
            ":quiche_common_mobile_quiche_logging_lib",
        ],
        "//conditions:default": ["@envoy//source/common/quic/platform:quiche_logging_impl_lib"],
    }),
)

envoy_cc_library(
    name = "quiche_common_platform_prefetch",
    hdrs = [
        "quiche/common/platform/api/quiche_prefetch.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_default_quiche_platform_impl_prefetch_impl_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_prefetch_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_prefetch_impl.h",
    ],
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_mobile_quiche_logging_lib",
    srcs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_logging_impl.cc",
    ],
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_logging_impl.h",
    ],
    deps = [
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/log:flags",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_mutex",
    srcs = [
        "quiche/common/platform/api/quiche_mutex.cc",
    ],
    hdrs = [
        "quiche/common/platform/api/quiche_mutex.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_default_quiche_platform_impl_mutex_impl_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_mutex_impl_lib",
    srcs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_mutex_impl.cc",
    ],
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_mutex_impl.h",
    ],
    deps = [
        ":quiche_common_platform_export",
        "@com_google_absl//absl/synchronization",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_hostname_utils",
    srcs = [
        "quiche/common/platform/api/quiche_hostname_utils.cc",
    ],
    hdrs = [
        "quiche/common/platform/api/quiche_hostname_utils.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
        ":quiche_common_platform_googleurl",
        ":quiche_common_platform_logging",
    ],
)

envoy_cc_test_library(
    name = "quiche_common_platform_thread",
    hdrs = [
        "quiche/common/platform/api/quiche_thread.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        "@envoy//test/common/quic/platform:quiche_thread_impl_lib",
    ],
)

envoy_cc_test_library(
    name = "quiche_common_platform_test_output",
    hdrs = [
        "quiche/common/platform/api/quiche_test_output.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        "@envoy//test/common/quic/platform:quiche_test_output_impl_lib",
    ],
)

envoy_cc_test_library(
    name = "quiche_common_platform_expect_bug",
    hdrs = [
        "quiche/common/platform/api/quiche_expect_bug.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        "@envoy//test/common/quic/platform:quiche_expect_bug_impl_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_udp_socket_platform",
    hdrs = select({
        "@envoy//bazel:linux": ["quiche/common/platform/api/quiche_udp_socket_platform_api.h"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_ip_address_family",
        ":quiche_common_platform_default_quiche_platform_impl_udp_socket_platform_impl_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_server_stats",
    hdrs = ["quiche/common/platform/api/quiche_server_stats.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_default_quiche_platform_impl_server_stats_impl_lib",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_server_stats_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_server_stats_impl.h",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_stack_trace",
    hdrs = [
        "quiche/common/platform/api/quiche_stack_trace.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
        "@envoy//source/common/quic/platform:quiche_stack_trace_impl_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_containers",
    hdrs = [
        "quiche/common/platform/api/quiche_containers.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_default_quiche_platform_impl_containers_impl_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_containers_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_containers_impl.h",
    ],
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_stream_buffer_allocator_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_stream_buffer_allocator_impl.h",
    ],
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_testvalue",
    hdrs = [
        "quiche/common/platform/api/quiche_testvalue.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform",
    hdrs = [
        "quiche/common/platform/api/quiche_bug_tracker.h",
        "quiche/common/platform/api/quiche_command_line_flags.h",
        "quiche/common/platform/api/quiche_flag_utils.h",
        "quiche/common/platform/api/quiche_flags.h",
        "quiche/common/platform/api/quiche_mem_slice.h",
        "quiche/common/platform/api/quiche_reference_counted.h",
        "quiche/common/platform/api/quiche_time_utils.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_bug_tracker",
        ":quiche_common_platform_default_quiche_platform_impl_command_line_flags_impl_lib",
        ":quiche_common_platform_default_quiche_platform_impl_flag_utils_impl_lib",
        ":quiche_common_platform_default_quiche_platform_impl_reference_counted_impl_lib",
        ":quiche_common_platform_default_quiche_platform_impl_testvalue_impl_lib",
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
        ":quiche_common_platform_prefetch",
        "@envoy//source/common/quic/platform:quic_base_impl_lib",
        "@envoy//source/common/quic/platform:quiche_flags_impl_lib",
        "@envoy//source/common/quic/platform:quiche_mem_slice_impl_lib",
        "@envoy//source/common/quic/platform:quiche_time_utils_impl_lib",
    ],
)

# Use the QUICHE default implementation once the WIN32 compiler error is resolved.
# envoy_quiche_platform_impl_cc_library(
#    name = "quiche_common_platform_default_quiche_platform_impl_export_impl_lib",
#    hdrs = [
#        "quiche/common/platform/default/quiche_platform_impl/quiche_export_impl.h",
#    ],
#)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_command_line_flags_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_command_line_flags_impl.h",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_flag_utils_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_flag_utils_impl.h",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_reference_counted_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_reference_counted_impl.h",
    ],
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_testvalue_impl_lib",
    hdrs = [
        "quiche/common/platform/default/quiche_platform_impl/quiche_testvalue_impl.h",
    ],
    deps = [
        ":quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_udp_socket_platform_impl_lib",
    hdrs = select({
        "@envoy//bazel:linux": ["quiche/common/platform/default/quiche_platform_impl/quiche_udp_socket_platform_impl.h"],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "quiche_common_platform_export",
    hdrs = [
        "quiche/common/platform/api/quiche_export.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "@envoy//source/common/quic/platform:quiche_export_impl_lib",
    ],
)

envoy_cc_test_library(
    name = "quiche_common_platform_test",
    hdrs = ["quiche/common/platform/api/quiche_test.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/common/quic/platform:quiche_test_impl_lib"],
)

envoy_cc_test(
    name = "quiche_common_mem_slice_test",
    srcs = ["quiche/common/platform/api/quiche_mem_slice_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_buffer_allocator_lib",
        ":quiche_common_platform",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test(
    name = "quiche_common_time_utils_test",
    srcs = ["quiche/common/platform/api/quiche_time_utils_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "quiche_common_print_elements_lib",
    hdrs = ["quiche/common/print_elements.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        "@com_google_absl//absl/container:inlined_vector",
    ],
)

envoy_cc_test_library(
    name = "quiche_common_test_tools_test_utils_lib",
    srcs = ["quiche/common/test_tools/quiche_test_utils.cc"],
    hdrs = [
        "quiche/common/test_tools/quiche_test_utils.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform",
        ":quiche_common_platform_googleurl",
        ":quiche_common_platform_iovec",
        ":quiche_common_platform_test",
        "@envoy//test/common/quic/platform:quiche_test_helpers_impl_lib",
        "@envoy//test/common/quic/platform:quiche_test_impl_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_text_utils_lib",
    srcs = ["quiche/common/quiche_text_utils.cc"],
    hdrs = ["quiche/common/quiche_text_utils.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/strings:str_format",
    ],
)

envoy_cc_library(
    name = "quiche_common_lib",
    srcs = [
        "quiche/common/quiche_data_reader.cc",
        "quiche/common/quiche_data_writer.cc",
    ],
    hdrs = [
        "quiche/common/quiche_data_reader.h",
        "quiche/common/quiche_data_writer.h",
        "quiche/common/quiche_linked_hash_map.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_endian_lib",
        ":quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "quiche_simple_arena_lib",
    srcs = ["quiche/common/quiche_simple_arena.cc"],
    hdrs = ["quiche/common/quiche_simple_arena.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
    ],
)

envoy_cc_library(
    name = "quiche_http_header_storage_lib",
    srcs = ["quiche/common/http/http_header_storage.cc"],
    hdrs = ["quiche/common/http/http_header_storage.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
        ":quiche_simple_arena_lib",
    ],
)

envoy_cc_library(
    name = "quiche_http_header_block_lib",
    srcs = ["quiche/common/http/http_header_block.cc"],
    hdrs = ["quiche/common/http/http_header_block.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_lib",
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
        ":quiche_common_text_utils_lib",
        ":quiche_http_header_storage_lib",
    ],
)

envoy_cc_test(
    name = "quiche_http_header_block_test",
    srcs = ["quiche/common/http/http_header_block_test.cc"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":http2_test_tools_test_utils_lib",
        ":quiche_common_platform_test",
        ":quiche_http_header_block_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_structured_headers_lib",
    srcs = ["quiche/common/structured_headers.cc"],
    hdrs = ["quiche/common/structured_headers.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
    ],
)

envoy_cc_test(
    name = "quiche_common_test",
    srcs = [
        "quiche/common/quiche_linked_hash_map_test.cc",
        "quiche/common/quiche_mem_slice_storage_test.cc",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_lib",
        ":quiche_common_mem_slice_storage",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test(
    name = "http2_platform_api_test",
    srcs = [
        "quiche/http2/test_tools/http2_random_test.cc",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":http2_test_tools_random",
        ":quiche_common_platform",
        ":quiche_common_test_tools_test_utils_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_mem_slice_storage",
    srcs = ["quiche/common/quiche_mem_slice_storage.cc"],
    hdrs = ["quiche/common/quiche_mem_slice_storage.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quiche_common_platform",
    ],
)

envoy_cc_test(
    name = "quic_core_batch_writer_batch_writer_test",
    srcs = select({
        "@envoy//bazel:linux": ["quiche/quic/core/batch_writer/quic_batch_writer_test.cc"],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_batch_writer_batch_writer_test_lib",
        ":quic_core_batch_writer_gso_batch_writer_lib",
        ":quic_core_batch_writer_sendmmsg_batch_writer_lib",
        ":quic_platform",
    ],
)

envoy_cc_library(
    name = "quic_load_balancer_server_id_lib",
    srcs = ["quiche/quic/load_balancer/load_balancer_server_id.cc"],
    hdrs = ["quiche/quic/load_balancer/load_balancer_server_id.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_bug_tracker",
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_load_balancer_config_lib",
    srcs = ["quiche/quic/load_balancer/load_balancer_config.cc"],
    hdrs = ["quiche/quic/load_balancer/load_balancer_config.h"],
    deps = [
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_load_balancer_server_id_lib",
        ":quic_platform_bug_tracker",
        ":quic_platform_export",
    ],
)

envoy_quic_cc_library(
    name = "quic_load_balancer_encoder_lib",
    srcs = ["quiche/quic/load_balancer/load_balancer_encoder.cc"],
    hdrs = ["quiche/quic/load_balancer/load_balancer_encoder.h"],
    deps = [
        ":quic_core_connection_id_generator_interface_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_data_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_load_balancer_config_lib",
        ":quic_load_balancer_server_id_lib",
        ":quic_platform_bug_tracker",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_lower_case_string",
    hdrs = ["quiche/common/platform/api/quiche_lower_case_string.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//source/common/quic/platform:quiche_lower_case_string_impl_lib"],
)

envoy_cc_library(
    name = "quiche_common_platform_header_policy",
    hdrs = ["quiche/common/platform/api/quiche_header_policy.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_default_quiche_platform_impl_header_policy_impl_lib"],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_common_platform_default_quiche_platform_impl_header_policy_impl_lib",
    hdrs = ["quiche/common/platform/default/quiche_platform_impl/quiche_header_policy_impl.h"],
)

envoy_cc_library(
    name = "quiche_balsa_balsa_enums_lib",
    srcs = ["quiche/balsa/balsa_enums.cc"],
    hdrs = ["quiche/balsa/balsa_enums.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quiche_common_platform_export"],
)

envoy_cc_library(
    name = "quiche_balsa_balsa_visitor_interface_lib",
    hdrs = ["quiche/balsa/balsa_visitor_interface.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_balsa_balsa_enums_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_noop_balsa_visitor_lib",
    hdrs = ["quiche/balsa/noop_balsa_visitor.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_balsa_balsa_visitor_interface_lib",
        ":quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_standard_header_map_lib",
    srcs = ["quiche/balsa/standard_header_map.cc"],
    hdrs = ["quiche/balsa/standard_header_map.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_text_utils_lib",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_framer_interface_lib",
    hdrs = ["quiche/balsa/framer_interface.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quiche_common_platform_export"],
)

envoy_cc_library(
    name = "quiche_balsa_http_validation_policy_lib",
    hdrs = ["quiche/balsa/http_validation_policy.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_header_api_lib",
    hdrs = ["quiche/balsa/header_api.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_callbacks",
        ":quiche_common_platform_export",
        ":quiche_common_platform_lower_case_string",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_simple_buffer_lib",
    srcs = ["quiche/balsa/simple_buffer.cc"],
    hdrs = ["quiche/balsa/simple_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform",
        ":quiche_common_platform_bug_tracker",
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "quiche_balsa_simple_buffer_test",
    srcs = ["quiche/balsa/simple_buffer_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_balsa_simple_buffer_lib",
        ":quiche_common_platform_expect_bug",
        ":quiche_common_platform_test",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_header_properties_lib",
    srcs = ["quiche/balsa/header_properties.cc"],
    hdrs = ["quiche/balsa/header_properties.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_platform_export",
        ":quiche_common_text_utils_lib",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "quiche_balsa_header_properties_test",
    srcs = ["quiche/balsa/header_properties_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_balsa_header_properties_lib",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_balsa_headers_lib",
    srcs = ["quiche/balsa/balsa_headers.cc"],
    hdrs = ["quiche/balsa/balsa_headers.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_balsa_balsa_enums_lib",
        ":quiche_balsa_header_api_lib",
        ":quiche_balsa_header_properties_lib",
        ":quiche_balsa_http_validation_policy_lib",
        ":quiche_balsa_standard_header_map_lib",
        ":quiche_common_callbacks",
        ":quiche_common_platform_bug_tracker",
        ":quiche_common_platform_export",
        ":quiche_common_platform_header_policy",
        ":quiche_common_platform_logging",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "quiche_balsa_balsa_headers_test",
    srcs = ["quiche/balsa/balsa_headers_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_balsa_balsa_enums_lib",
        ":quiche_balsa_balsa_frame_lib",
        ":quiche_balsa_balsa_headers_lib",
        ":quiche_balsa_simple_buffer_lib",
        ":quiche_common_platform_expect_bug",
        ":quiche_common_platform_logging",
        ":quiche_common_platform_test",
        ":quiche_common_test_tools_test_utils_lib",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

envoy_cc_library(
    name = "quiche_balsa_balsa_frame_lib",
    srcs = ["quiche/balsa/balsa_frame.cc"],
    hdrs = ["quiche/balsa/balsa_frame.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_balsa_balsa_enums_lib",
        ":quiche_balsa_balsa_headers_lib",
        ":quiche_balsa_balsa_visitor_interface_lib",
        ":quiche_balsa_framer_interface_lib",
        ":quiche_balsa_header_properties_lib",
        ":quiche_balsa_http_validation_policy_lib",
        ":quiche_balsa_noop_balsa_visitor_lib",
        ":quiche_common_platform",
        ":quiche_common_platform_bug_tracker",
        ":quiche_common_platform_export",
        ":quiche_common_platform_logging",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "quiche_balsa_balsa_frame_test",
    srcs = ["quiche/balsa/balsa_frame_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":quiche_balsa_balsa_enums_lib",
        ":quiche_balsa_balsa_frame_lib",
        ":quiche_balsa_balsa_headers_lib",
        ":quiche_balsa_balsa_visitor_interface_lib",
        ":quiche_balsa_http_validation_policy_lib",
        ":quiche_balsa_noop_balsa_visitor_lib",
        ":quiche_balsa_simple_buffer_lib",
        ":quiche_common_platform_expect_bug",
        ":quiche_common_platform_logging",
        ":quiche_common_platform_test",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

envoy_cc_library(
    name = "quiche_web_transport_web_transport_lib",
    hdrs = ["quiche/web_transport/web_transport.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":common_http_http_header_block_lib",
        ":quiche_common_callbacks",
        ":quiche_common_platform_export",
        ":quiche_common_quiche_stream_lib",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

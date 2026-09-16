load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_enable_http3",
)
load("//bazel/external:quiche.bzl", "quiche_copts")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "oghttp2_session_trailers_test",
    srcs = envoy_select_enable_http3(["oghttp2_session_trailers_test.cc"]),
    copts = quiche_copts,
    rbe_pool = "6gig",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_mock_http2_visitor",
        "@quiche//:http2_adapter_oghttp2_adapter",
        "@quiche//:http2_adapter_test_frame_sequence",
        "@quiche//:http2_adapter_test_utils",
        "@quiche//:quiche_common_platform_test",
    ]),
)

# QUICHE test sources compiled under Envoy's real envoy_cc_test macro, which
# injects the correct test main, PCH, sanitizer configuration, and gmock
# strictness settings. The test sources are exposed as filegroups in
# @quiche//:*_srcs and deps point at the corresponding @quiche// libraries.

envoy_cc_test(
    name = "http2_adapter_event_forwarder_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_event_forwarder_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_event_forwarder",
        "@quiche//:http2_core_protocol_lib",
        "@quiche//:http2_test_tools_mock_spdy_framer_visitor_lib",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_header_validator_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_header_validator_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_header_validator",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_impl_comparison_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_impl_comparison_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter",
        "@quiche//:http2_adapter_http2_protocol",
        "@quiche//:http2_adapter_mock_http2_visitor",
        "@quiche//:http2_adapter_recording_http2_visitor",
        "@quiche//:http2_adapter_test_frame_sequence",
        "@quiche//:http2_adapter_test_utils",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_nghttp2_adapter_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_nghttp2_adapter_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_http2_protocol",
        "@quiche//:http2_adapter_http2_visitor_interface",
        "@quiche//:http2_adapter_mock_http2_visitor",
        "@quiche//:http2_adapter_nghttp2_adapter",
        "@quiche//:http2_adapter_nghttp2_include",
        "@quiche//:http2_adapter_nghttp2_test_utils",
        "@quiche//:http2_adapter_oghttp2_util",
        "@quiche//:http2_adapter_test_frame_sequence",
        "@quiche//:http2_adapter_test_utils",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_nghttp2_data_provider_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_nghttp2_data_provider_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_nghttp2_data_provider",
        "@quiche//:http2_adapter_test_utils",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_nghttp2_session_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_nghttp2_session_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter",
        "@quiche//:http2_adapter_mock_http2_visitor",
        "@quiche//:http2_adapter_nghttp2_callbacks",
        "@quiche//:http2_adapter_nghttp2_util",
        "@quiche//:http2_adapter_test_frame_sequence",
        "@quiche//:http2_adapter_test_utils",
        "@quiche//:quiche_common_platform_expect_bug",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_oghttp2_adapter_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_oghttp2_adapter_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_http2_protocol",
        "@quiche//:http2_adapter_http2_visitor_interface",
        "@quiche//:http2_adapter_mock_http2_visitor",
        "@quiche//:http2_adapter_oghttp2_adapter",
        "@quiche//:http2_adapter_oghttp2_util",
        "@quiche//:http2_adapter_test_frame_sequence",
        "@quiche//:http2_adapter_test_utils",
        "@quiche//:quiche_common_platform_expect_bug",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_oghttp2_session_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_oghttp2_session_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_mock_http2_visitor",
        "@quiche//:http2_adapter_oghttp2_adapter",
        "@quiche//:http2_adapter_test_frame_sequence",
        "@quiche//:http2_adapter_test_utils",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_oghttp2_util_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_oghttp2_util_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_http2_protocol",
        "@quiche//:http2_adapter_oghttp2_util",
        "@quiche//:http2_adapter_test_frame_sequence",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_recording_http2_visitor_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_recording_http2_visitor_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_http2_protocol",
        "@quiche//:http2_adapter_http2_visitor_interface",
        "@quiche//:http2_adapter_recording_http2_visitor",
        "@quiche//:http2_test_tools_random",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "http2_adapter_window_manager_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_adapter_window_manager_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_adapter_window_manager",
        "@quiche//:http2_test_tools_random",
        "@quiche//:quiche_common_platform_expect_bug",
        "@quiche//:quiche_common_platform_export",
        "@quiche//:quiche_common_platform_test",
        "@abseil-cpp//absl/functional:bind_front",
    ]),
)

envoy_cc_test(
    name = "http2_platform_api_test",
    srcs = envoy_select_enable_http3(["@quiche//:http2_platform_api_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_test_tools_random",
        "@quiche//:quiche_common_platform",
        "@quiche//:quiche_common_test_tools_test_utils_lib",
    ]),
)

envoy_cc_test(
    name = "quiche_balsa_balsa_frame_test",
    srcs = envoy_select_enable_http3(["@quiche//:quiche_balsa_balsa_frame_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:quiche_balsa_balsa_enums_lib",
        "@quiche//:quiche_balsa_balsa_frame_lib",
        "@quiche//:quiche_balsa_balsa_headers_lib",
        "@quiche//:quiche_balsa_balsa_visitor_interface_lib",
        "@quiche//:quiche_balsa_http_validation_policy_lib",
        "@quiche//:quiche_balsa_noop_balsa_visitor_lib",
        "@quiche//:quiche_balsa_simple_buffer_lib",
        "@quiche//:quiche_common_platform_expect_bug",
        "@quiche//:quiche_common_platform_logging",
        "@quiche//:quiche_common_platform_test",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ]),
)

envoy_cc_test(
    name = "quiche_balsa_balsa_headers_test",
    srcs = envoy_select_enable_http3(["@quiche//:quiche_balsa_balsa_headers_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:quiche_balsa_balsa_enums_lib",
        "@quiche//:quiche_balsa_balsa_frame_lib",
        "@quiche//:quiche_balsa_balsa_headers_lib",
        "@quiche//:quiche_balsa_simple_buffer_lib",
        "@quiche//:quiche_common_platform_expect_bug",
        "@quiche//:quiche_common_platform_logging",
        "@quiche//:quiche_common_platform_test",
        "@quiche//:quiche_common_test_tools_test_utils_lib",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ]),
)

envoy_cc_test(
    name = "quiche_balsa_header_properties_test",
    srcs = envoy_select_enable_http3(["@quiche//:quiche_balsa_header_properties_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:quiche_balsa_header_properties_lib",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "quiche_balsa_simple_buffer_test",
    srcs = envoy_select_enable_http3(["@quiche//:quiche_balsa_simple_buffer_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:quiche_balsa_simple_buffer_lib",
        "@quiche//:quiche_common_platform_expect_bug",
        "@quiche//:quiche_common_platform_test",
        "@abseil-cpp//absl/strings",
    ]),
)

envoy_cc_test(
    name = "quiche_common_test",
    srcs = envoy_select_enable_http3(["@quiche//:quiche_common_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:quiche_common_lib",
        "@quiche//:quiche_common_mem_slice_storage",
        "@quiche//:quiche_common_platform_test",
    ]),
)

envoy_cc_test(
    name = "quiche_http_header_block_test",
    srcs = envoy_select_enable_http3(["@quiche//:quiche_http_header_block_test_srcs"]),
    copts = quiche_copts,
    rbe_pool = "linux_x64_small",
    tags = ["quiche"],
    deps = envoy_select_enable_http3([
        "@quiche//:http2_test_tools_test_utils_lib",
        "@quiche//:quiche_common_platform_test",
        "@quiche//:quiche_http_header_block_lib",
        "@abseil-cpp//absl/functional:bind_front",
    ]),
)

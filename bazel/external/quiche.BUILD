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
# source files in group 3 (the Platform impl). Unfortunately, QUICHE does not
# yet provide a built-in way to customize this dependency, e.g. to override the
# directory or namespace in which Platform impl types are defined. Hence the
# gross hacks in quiche.genrule_cmd, invoked from here to tweak QUICHE source
# files into a form usable by Envoy.
#
# The mechanics of this will change as QUICHE evolves, supplies its own Bazel
# buildfiles, and provides a built-in way to override platform impl directory
# location. However, the end result (QUICHE files placed under
# quiche/{http2,quic,spdy}/, with the Envoy-specific implementation of the
# QUICHE platform APIs in //source/extensions/quic_listeners/quiche/platform/,
# should remain largely the same.

load(":genrule_cmd.bzl", "genrule_cmd")
load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_copts",
    "envoy_select_quiche",
)

src_files = glob([
    "**/*.h",
    "**/*.c",
    "**/*.cc",
    "**/*.inc",
    "**/*.proto",
])

genrule(
    name = "quiche_files",
    srcs = src_files,
    outs = ["quiche/" + f for f in src_files],
    cmd = genrule_cmd("@envoy//bazel/external:quiche.genrule_cmd"),
    visibility = ["//visibility:private"],
)

cc_library(
    name = "http2_platform",
    hdrs = [
        "quiche/http2/platform/api/http2_arraysize.h",
        "quiche/http2/platform/api/http2_containers.h",
        "quiche/http2/platform/api/http2_estimate_memory_usage.h",
        "quiche/http2/platform/api/http2_export.h",
        "quiche/http2/platform/api/http2_flag_utils.h",
        "quiche/http2/platform/api/http2_macros.h",
        "quiche/http2/platform/api/http2_optional.h",
        "quiche/http2/platform/api/http2_ptr_util.h",
        "quiche/http2/platform/api/http2_string.h",
        "quiche/http2/platform/api/http2_string_piece.h",
        # TODO: uncomment the following files as implementations are added.
        # "quiche/http2/platform/api/http2_flags.h",
        # "quiche/http2/platform/api/http2_reconstruct_object.h",
        # "quiche/http2/platform/api/http2_test_helpers.h",
    ] + envoy_select_quiche(
        [
            "quiche/http2/platform/api/http2_bug_tracker.h",
            "quiche/http2/platform/api/http2_logging.h",
            "quiche/http2/platform/api/http2_string_utils.h",
        ],
        "@envoy",
    ),
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:http2_platform_impl_lib"],
)

cc_library(
    name = "spdy_platform",
    hdrs = [
        "quiche/spdy/platform/api/spdy_arraysize.h",
        "quiche/spdy/platform/api/spdy_containers.h",
        "quiche/spdy/platform/api/spdy_endianness_util.h",
        "quiche/spdy/platform/api/spdy_estimate_memory_usage.h",
        "quiche/spdy/platform/api/spdy_export.h",
        "quiche/spdy/platform/api/spdy_mem_slice.h",
        "quiche/spdy/platform/api/spdy_ptr_util.h",
        "quiche/spdy/platform/api/spdy_string.h",
        "quiche/spdy/platform/api/spdy_string_piece.h",
        # TODO: uncomment the following files as implementations are added.
        # "quiche/spdy/platform/api/spdy_flags.h",
    ] + envoy_select_quiche(
        [
            "quiche/spdy/platform/api/spdy_bug_tracker.h",
            "quiche/spdy/platform/api/spdy_logging.h",
            "quiche/spdy/platform/api/spdy_string_utils.h",
        ],
        "@envoy",
    ),
    visibility = ["//visibility:public"],
    deps = [
        ":quic_buffer_allocator_lib",
        "@envoy//source/extensions/quic_listeners/quiche/platform:spdy_platform_impl_lib",
    ],
)

cc_library(
    name = "quic_platform",
    srcs = ["quiche/quic/platform/api/quic_mutex.cc"] + envoy_select_quiche(
        [
            "quiche/quic/platform/api/quic_file_utils.cc",
            "quiche/quic/platform/api/quic_hostname_utils.cc",
        ],
        "@envoy",
    ),
    hdrs = [
        "quiche/quic/platform/api/quic_cert_utils.h",
        "quiche/quic/platform/api/quic_mutex.h",
        "quiche/quic/platform/api/quic_str_cat.h",
    ] + envoy_select_quiche(
        [
            "quiche/quic/platform/api/quic_file_utils.h",
            "quiche/quic/platform/api/quic_hostname_utils.h",
        ],
        "@envoy",
    ),
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        "@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_impl_lib",
    ],
)

cc_library(
    name = "quic_platform_export",
    hdrs = ["quiche/quic/platform/api/quic_export.h"],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_export_impl_lib"],
)

cc_library(
    name = "quic_platform_ip_address_family",
    hdrs = ["quiche/quic/platform/api/quic_ip_address_family.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "quic_platform_port_utils",
    testonly = 1,
    hdrs = envoy_select_quiche(
        ["quiche/quic/platform/api/quic_port_utils.h"],
        "@envoy",
    ),
    visibility = ["//visibility:public"],
    deps = envoy_select_quiche(
        ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_port_utils_impl_lib"],
        "@envoy",
    ),
)

cc_library(
    name = "quic_platform_base",
    srcs = envoy_select_quiche(
        [
            "quiche/quic/platform/api/quic_ip_address.cc",
            "quiche/quic/platform/api/quic_socket_address.cc",
        ],
        "@envoy",
    ),
    hdrs = [
        "quiche/quic/platform/api/quic_aligned.h",
        "quiche/quic/platform/api/quic_arraysize.h",
        "quiche/quic/platform/api/quic_client_stats.h",
        "quiche/quic/platform/api/quic_containers.h",
        "quiche/quic/platform/api/quic_endian.h",
        "quiche/quic/platform/api/quic_estimate_memory_usage.h",
        "quiche/quic/platform/api/quic_exported_stats.h",
        "quiche/quic/platform/api/quic_fallthrough.h",
        "quiche/quic/platform/api/quic_flag_utils.h",
        "quiche/quic/platform/api/quic_flags.h",
        "quiche/quic/platform/api/quic_iovec.h",
        "quiche/quic/platform/api/quic_ip_address.h",
        "quiche/quic/platform/api/quic_map_util.h",
        "quiche/quic/platform/api/quic_mem_slice.h",
        "quiche/quic/platform/api/quic_prefetch.h",
        "quiche/quic/platform/api/quic_ptr_util.h",
        "quiche/quic/platform/api/quic_reference_counted.h",
        "quiche/quic/platform/api/quic_server_stats.h",
        "quiche/quic/platform/api/quic_socket_address.h",
        "quiche/quic/platform/api/quic_stream_buffer_allocator.h",
        "quiche/quic/platform/api/quic_string_piece.h",
        "quiche/quic/platform/api/quic_test_output.h",
        "quiche/quic/platform/api/quic_uint128.h",
        # TODO: uncomment the following files as implementations are added.
        # "quiche/quic/platform/api/quic_clock.h",
        # "quiche/quic/platform/api/quic_fuzzed_data_provider.h",
        # "quiche/quic/platform/api/quic_goog_cc_sender.h",
        # "quiche/quic/platform/api/quic_ip_address_family.h",
        # "quiche/quic/platform/api/quic_lru_cache.h",
        # "quiche/quic/platform/api/quic_pcc_sender.h",
        # "quiche/quic/platform/api/quic_test_loopback.h",
    ] + envoy_select_quiche(
        [
            "quiche/quic/platform/api/quic_bug_tracker.h",
            "quiche/quic/platform/api/quic_expect_bug.h",
            "quiche/quic/platform/api/quic_mock_log.h",
            "quiche/quic/platform/api/quic_logging.h",
            "quiche/quic/platform/api/quic_stack_trace.h",
            "quiche/quic/platform/api/quic_string_utils.h",
            "quiche/quic/platform/api/quic_test.h",
            "quiche/quic/platform/api/quic_text_utils.h",
            "quiche/quic/platform/api/quic_thread.h",
        ],
        "@envoy",
    ),
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_export",
        "@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_base_impl_lib",
    ],
)

cc_library(
    name = "quic_platform_sleep",
    hdrs = ["quiche/quic/platform/api/quic_sleep.h"],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_sleep_impl_lib"],
)

cc_library(
    name = "quic_time_lib",
    srcs = ["quiche/quic/core/quic_time.cc"],
    hdrs = ["quiche/quic/core/quic_time.h"],
    visibility = ["//visibility:public"],
    deps = [":quic_platform"],
)

cc_library(
    name = "quic_buffer_allocator_lib",
    srcs = [
        "quiche/quic/core/quic_buffer_allocator.cc",
        "quiche/quic/core/quic_simple_buffer_allocator.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_buffer_allocator.h",
        "quiche/quic/core/quic_simple_buffer_allocator.h",
    ],
    visibility = ["//visibility:public"],
    deps = [":quic_platform_export"],
)

envoy_cc_test(
    name = "http2_platform_test",
    srcs = envoy_select_quiche(
        ["quiche/http2/platform/api/http2_string_utils_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [":http2_platform"],
)

envoy_cc_test(
    name = "spdy_platform_test",
    srcs = envoy_select_quiche(
        ["quiche/spdy/platform/api/spdy_string_utils_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [":spdy_platform"],
)

cc_library(
    name = "quic_platform_mem_slice_span_lib",
    hdrs = [
        "quiche/quic/platform/api/quic_mem_slice_span.h",
    ],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_mem_slice_span_impl_lib"],
)

cc_library(
    name = "quic_platform_test_mem_slice_vector_lib",
    testonly = 1,
    hdrs = ["quiche/quic/platform/api/quic_test_mem_slice_vector.h"],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_test_mem_slice_vector_impl_lib"],
)

cc_library(
    name = "quic_platform_mem_slice_storage_lib",
    hdrs = ["quiche/quic/platform/api/quic_mem_slice_storage.h"],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_mem_slice_storage_impl_lib"],
)

cc_library(
    name = "quiche_quic_core_base",
    srcs = envoy_select_quiche(
        [
            "quiche/quic/core/crypto/quic_random.cc",
            "quiche/quic/core/quic_connection_id.cc",
            "quiche/quic/core/quic_constants.cc",
            "quiche/quic/core/quic_error_codes.cc",
            "quiche/quic/core/quic_packet_number.cc",
            "quiche/quic/core/quic_tag.cc",
            "quiche/quic/core/quic_types.cc",
            "quiche/quic/core/quic_versions.cc",
        ],
        "@envoy",
    ),
    hdrs = envoy_select_quiche(
        [
            "quiche/quic/core/crypto/quic_random.h",
            "quiche/quic/core/quic_connection_id.h",
            "quiche/quic/core/quic_constants.h",
            "quiche/quic/core/quic_error_codes.h",
            "quiche/quic/core/quic_interval.h",
            "quiche/quic/core/quic_interval_set.h",
            "quiche/quic/core/quic_packet_number.h",
            "quiche/quic/core/quic_tag.h",
            "quiche/quic/core/quic_types.h",
            "quiche/quic/core/quic_versions.h",
        ],
        "@envoy",
    ),
    # Need to use same compiler options as envoy_cc_library uses to enforce compiler version and c++ version.
    copts = envoy_copts("@envoy"),
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        ":quic_time_lib",
        "//external:ssl",
    ],
)

cc_library(
    name = "quiche_quic_core_frames",
    srcs = envoy_select_quiche(
        [
            "quiche/quic/core/frames/quic_ack_frame.cc",
            "quiche/quic/core/frames/quic_application_close_frame.cc",
            "quiche/quic/core/frames/quic_blocked_frame.cc",
            "quiche/quic/core/frames/quic_connection_close_frame.cc",
            "quiche/quic/core/frames/quic_crypto_frame.cc",
            "quiche/quic/core/frames/quic_frame.cc",
            "quiche/quic/core/frames/quic_goaway_frame.cc",
            "quiche/quic/core/frames/quic_max_stream_id_frame.cc",
            "quiche/quic/core/frames/quic_message_frame.cc",
            "quiche/quic/core/frames/quic_new_connection_id_frame.cc",
            "quiche/quic/core/frames/quic_new_token_frame.cc",
            "quiche/quic/core/frames/quic_padding_frame.cc",
            "quiche/quic/core/frames/quic_path_challenge_frame.cc",
            "quiche/quic/core/frames/quic_path_response_frame.cc",
            "quiche/quic/core/frames/quic_ping_frame.cc",
            "quiche/quic/core/frames/quic_retire_connection_id_frame.cc",
            "quiche/quic/core/frames/quic_rst_stream_frame.cc",
            "quiche/quic/core/frames/quic_stop_sending_frame.cc",
            "quiche/quic/core/frames/quic_stop_waiting_frame.cc",
            "quiche/quic/core/frames/quic_stream_frame.cc",
            "quiche/quic/core/frames/quic_stream_id_blocked_frame.cc",
            "quiche/quic/core/frames/quic_window_update_frame.cc",
        ],
        "@envoy",
    ),
    hdrs = envoy_select_quiche(
        [
            "quiche/quic/core/frames/quic_ack_frame.h",
            "quiche/quic/core/frames/quic_application_close_frame.h",
            "quiche/quic/core/frames/quic_blocked_frame.h",
            "quiche/quic/core/frames/quic_connection_close_frame.h",
            "quiche/quic/core/frames/quic_crypto_frame.h",
            "quiche/quic/core/frames/quic_frame.h",
            "quiche/quic/core/frames/quic_goaway_frame.h",
            "quiche/quic/core/frames/quic_inlined_frame.h",
            "quiche/quic/core/frames/quic_max_stream_id_frame.h",
            "quiche/quic/core/frames/quic_message_frame.h",
            "quiche/quic/core/frames/quic_mtu_discovery_frame.h",
            "quiche/quic/core/frames/quic_new_connection_id_frame.h",
            "quiche/quic/core/frames/quic_new_token_frame.h",
            "quiche/quic/core/frames/quic_padding_frame.h",
            "quiche/quic/core/frames/quic_path_challenge_frame.h",
            "quiche/quic/core/frames/quic_path_response_frame.h",
            "quiche/quic/core/frames/quic_ping_frame.h",
            "quiche/quic/core/frames/quic_retire_connection_id_frame.h",
            "quiche/quic/core/frames/quic_rst_stream_frame.h",
            "quiche/quic/core/frames/quic_stop_sending_frame.h",
            "quiche/quic/core/frames/quic_stop_waiting_frame.h",
            "quiche/quic/core/frames/quic_stream_frame.h",
            "quiche/quic/core/frames/quic_stream_id_blocked_frame.h",
            "quiche/quic/core/frames/quic_window_update_frame.h",
        ],
        "@envoy",
    ),
    # Need to use same compiler options as envoy_cc_library uses to enforce compiler version and c++ version.
    # QUIC uses offsetof() to optimize memory usage in frames.
    copts = envoy_copts("@envoy") + [
        "-Wno-error=invalid-offsetof",
        "-Wno-error=unused-parameter",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        ":quic_platform_mem_slice_span_lib",
        ":quiche_quic_core_base",
    ],
)

cc_library(
    name = "quiche_quic_core",
    srcs = envoy_select_quiche(
        [
            "quiche/quic/core/quic_ack_listener_interface.cc",
            "quiche/quic/core/quic_bandwidth.cc",
            "quiche/quic/core/quic_data_writer.cc",
            "quiche/quic/core/quic_stream_send_buffer.cc",
            "quiche/quic/core/quic_utils.cc",
        ],
        "@envoy",
    ),
    hdrs = envoy_select_quiche(
        [
            "quiche/quic/core/quic_ack_listener_interface.h",
            "quiche/quic/core/quic_bandwidth.h",
            "quiche/quic/core/quic_data_writer.h",
            "quiche/quic/core/quic_stream_send_buffer.h",
            "quiche/quic/core/quic_utils.h",
        ],
        "@envoy",
    ),
    copts = envoy_copts("@envoy") + ["-Wno-error=invalid-offsetof"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_quic_core_base",
        ":quiche_quic_core_frames",
    ],
)

envoy_cc_test(
    name = "quic_platform_test",
    srcs = envoy_select_quiche(
        [
            "quiche/quic/platform/api/quic_mem_slice_span_test.cc",
            "quiche/quic/platform/api/quic_mem_slice_storage_test.cc",
            "quiche/quic/platform/api/quic_mem_slice_test.cc",
            "quiche/quic/platform/api/quic_reference_counted_test.cc",
            "quiche/quic/platform/api/quic_string_utils_test.cc",
            "quiche/quic/platform/api/quic_text_utils_test.cc",
        ],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [
        ":quic_buffer_allocator_lib",
        ":quic_platform",
        ":quic_platform_mem_slice_span_lib",
        ":quic_platform_mem_slice_storage_lib",
        ":quic_platform_test_mem_slice_vector_lib",
    ],
)

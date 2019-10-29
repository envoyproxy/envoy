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
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_proto_library",
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

# These options are only used to suppress errors in brought-in QUICHE tests.
# Use #pragma GCC diagnostic ignored in integration code to suppress these errors.
quiche_copts = select({
    "@envoy//bazel:windows_x86_64": [],
    "//conditions:default": [
        # Remove these after upstream fix.
        "-Wno-unused-parameter",
        "-Wno-unused-function",
        "-Wno-type-limits",
        # quic_inlined_frame.h uses offsetof() to optimize memory usage in frames.
        "-Wno-invalid-offsetof",
        "-Wno-type-limits",
        "-Wno-return-type",
    ],
})

envoy_cc_test_library(
    name = "http2_platform_reconstruct_object",
    hdrs = ["quiche/http2/platform/api/http2_reconstruct_object.h"],
    repository = "@envoy",
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:http2_platform_reconstruct_object_impl_lib"],
)

envoy_cc_test_library(
    name = "http2_test_tools_random",
    srcs = ["quiche/http2/test_tools/http2_random.cc"],
    hdrs = ["quiche/http2/test_tools/http2_random.h"],
    external_deps = ["ssl"],
    repository = "@envoy",
    deps = [":http2_platform"],
)

envoy_cc_library(
    name = "http2_platform",
    hdrs = [
        "quiche/http2/platform/api/http2_arraysize.h",
        "quiche/http2/platform/api/http2_bug_tracker.h",
        "quiche/http2/platform/api/http2_containers.h",
        "quiche/http2/platform/api/http2_estimate_memory_usage.h",
        "quiche/http2/platform/api/http2_export.h",
        "quiche/http2/platform/api/http2_flag_utils.h",
        "quiche/http2/platform/api/http2_flags.h",
        "quiche/http2/platform/api/http2_logging.h",
        "quiche/http2/platform/api/http2_macros.h",
        "quiche/http2/platform/api/http2_optional.h",
        "quiche/http2/platform/api/http2_ptr_util.h",
        "quiche/http2/platform/api/http2_string.h",
        "quiche/http2/platform/api/http2_string_piece.h",
        "quiche/http2/platform/api/http2_string_utils.h",
        # TODO: uncomment the following files as implementations are added.
        # "quiche/http2/platform/api/http2_test_helpers.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:http2_platform_impl_lib"],
)

envoy_cc_library(
    name = "http2_constants_lib",
    srcs = ["quiche/http2/http2_constants.cc"],
    hdrs = ["quiche/http2/http2_constants.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
)

envoy_cc_library(
    name = "http2_structures_lib",
    srcs = ["quiche/http2/http2_structures.cc"],
    hdrs = ["quiche/http2/http2_structures.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_platform",
    ],
)

envoy_cc_library(
    name = "http2_decoder_decode_buffer_lib",
    srcs = ["quiche/http2/decoder/decode_buffer.cc"],
    hdrs = ["quiche/http2/decoder/decode_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
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
        ":http2_platform",
        ":http2_structures_lib",
    ],
)

envoy_cc_library(
    name = "http2_decoder_decode_status_lib",
    srcs = ["quiche/http2/decoder/decode_status.cc"],
    hdrs = ["quiche/http2/decoder/decode_status.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_decoder_payload_decoders_push_promise_payload_decoder_lib",
        ":http2_decoder_payload_decoders_rst_stream_payload_decoder_lib",
        ":http2_decoder_payload_decoders_settings_payload_decoder_lib",
        ":http2_decoder_payload_decoders_unknown_payload_decoder_lib",
        ":http2_decoder_payload_decoders_window_update_payload_decoder_lib",
        ":http2_decoder_structure_decoder_lib",
        ":http2_hpack_varint_hpack_varint_decoder_lib",
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_platform",
        ":http2_structures_lib",
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
        ":http2_hpack_decoder_hpack_entry_decoder_lib",
        ":http2_hpack_decoder_hpack_entry_decoder_listener_lib",
        ":http2_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_decoder_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_decoder.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_decoder_hpack_block_decoder_lib",
        ":http2_hpack_decoder_hpack_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_decoder_state_lib",
        ":http2_hpack_decoder_hpack_decoder_tables_lib",
        ":http2_hpack_decoder_hpack_whole_entry_buffer_lib",
        ":http2_platform",
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
        ":http2_hpack_hpack_string_lib",
        ":http2_platform",
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
        ":http2_hpack_decoder_hpack_whole_entry_listener_lib",
        ":http2_hpack_hpack_constants_lib",
        ":http2_hpack_hpack_string_lib",
        ":http2_platform",
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
        ":http2_platform",
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
        ":http2_hpack_hpack_string_lib",
        ":http2_platform",
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
        ":http2_hpack_decoder_hpack_entry_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_entry_type_decoder_lib",
        ":http2_hpack_decoder_hpack_string_decoder_lib",
        ":http2_hpack_hpack_constants_lib",
        ":http2_platform",
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
        ":http2_platform",
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
        ":http2_platform",
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
        ":http2_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_string_decoder_listener_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_string_decoder_listener.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_string_decoder_listener.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
)

envoy_cc_library(
    name = "http2_hpack_decoder_hpack_whole_entry_buffer_lib",
    srcs = ["quiche/http2/hpack/decoder/hpack_whole_entry_buffer.cc"],
    hdrs = ["quiche/http2/hpack/decoder/hpack_whole_entry_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_decoder_hpack_decoder_string_buffer_lib",
        ":http2_hpack_decoder_hpack_entry_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_whole_entry_listener_lib",
        ":http2_hpack_hpack_constants_lib",
        ":http2_platform",
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
        ":http2_hpack_hpack_constants_lib",
        ":http2_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_huffman_hpack_huffman_decoder_lib",
    srcs = ["quiche/http2/hpack/huffman/hpack_huffman_decoder.cc"],
    hdrs = ["quiche/http2/hpack/huffman/hpack_huffman_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
)

envoy_cc_library(
    name = "http2_hpack_huffman_hpack_huffman_encoder_lib",
    srcs = ["quiche/http2/hpack/huffman/hpack_huffman_encoder.cc"],
    hdrs = ["quiche/http2/hpack/huffman/hpack_huffman_encoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_hpack_huffman_huffman_spec_tables_lib",
        ":http2_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_huffman_huffman_spec_tables_lib",
    srcs = ["quiche/http2/hpack/huffman/huffman_spec_tables.cc"],
    hdrs = ["quiche/http2/hpack/huffman/huffman_spec_tables.h"],
    copts = quiche_copts,
    repository = "@envoy",
)

envoy_cc_library(
    name = "http2_hpack_hpack_constants_lib",
    srcs = ["quiche/http2/hpack/http2_hpack_constants.cc"],
    hdrs = ["quiche/http2/hpack/http2_hpack_constants.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
)

envoy_cc_library(
    name = "http2_hpack_hpack_static_table_entries_lib",
    hdrs = ["quiche/http2/hpack/hpack_static_table_entries.inc"],
    repository = "@envoy",
)

envoy_cc_library(
    name = "http2_hpack_hpack_string_lib",
    srcs = ["quiche/http2/hpack/hpack_string.cc"],
    hdrs = ["quiche/http2/hpack/hpack_string.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
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
        ":http2_platform",
    ],
)

envoy_cc_library(
    name = "http2_hpack_varint_hpack_varint_encoder_lib",
    srcs = ["quiche/http2/hpack/varint/hpack_varint_encoder.cc"],
    hdrs = ["quiche/http2/hpack/varint/hpack_varint_encoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":http2_platform"],
)

envoy_cc_library(
    name = "spdy_platform",
    hdrs = [
        "quiche/spdy/platform/api/spdy_arraysize.h",
        "quiche/spdy/platform/api/spdy_bug_tracker.h",
        "quiche/spdy/platform/api/spdy_containers.h",
        "quiche/spdy/platform/api/spdy_endianness_util.h",
        "quiche/spdy/platform/api/spdy_estimate_memory_usage.h",
        "quiche/spdy/platform/api/spdy_export.h",
        "quiche/spdy/platform/api/spdy_flags.h",
        "quiche/spdy/platform/api/spdy_logging.h",
        "quiche/spdy/platform/api/spdy_macros.h",
        "quiche/spdy/platform/api/spdy_map_util.h",
        "quiche/spdy/platform/api/spdy_mem_slice.h",
        "quiche/spdy/platform/api/spdy_ptr_util.h",
        "quiche/spdy/platform/api/spdy_string.h",
        "quiche/spdy/platform/api/spdy_string_piece.h",
        "quiche/spdy/platform/api/spdy_string_utils.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_common_lib",
        "@envoy//source/extensions/quic_listeners/quiche/platform:spdy_platform_impl_lib",
    ],
)

envoy_cc_library(
    name = "spdy_simple_arena_lib",
    srcs = ["quiche/spdy/core/spdy_simple_arena.cc"],
    hdrs = ["quiche/spdy/core/spdy_simple_arena.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [":spdy_platform"],
)

envoy_cc_test_library(
    name = "spdy_platform_test",
    hdrs = ["quiche/spdy/platform/api/spdy_test.h"],
    repository = "@envoy",
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:spdy_platform_test_impl_lib"],
)

envoy_cc_test_library(
    name = "spdy_platform_test_helpers",
    hdrs = ["quiche/spdy/platform/api/spdy_test_helpers.h"],
    repository = "@envoy",
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:spdy_platform_test_helpers_impl_lib"],
)

envoy_cc_library(
    name = "spdy_platform_unsafe_arena_lib",
    hdrs = ["quiche/spdy/platform/api/spdy_unsafe_arena.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:spdy_platform_unsafe_arena_impl_lib"],
)

envoy_cc_library(
    name = "spdy_core_alt_svc_wire_format_lib",
    srcs = ["quiche/spdy/core/spdy_alt_svc_wire_format.cc"],
    hdrs = ["quiche/spdy/core/spdy_alt_svc_wire_format.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [":spdy_platform"],
)

envoy_cc_library(
    name = "spdy_core_fifo_write_scheduler_lib",
    hdrs = ["quiche/spdy/core/fifo_write_scheduler.h"],
    repository = "@envoy",
    deps = [
        ":spdy_core_write_scheduler_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_framer_lib",
    srcs = [
        "quiche/spdy/core/spdy_frame_builder.cc",
        "quiche/spdy/core/spdy_framer.cc",
    ],
    hdrs = [
        "quiche/spdy/core/spdy_frame_builder.h",
        "quiche/spdy/core/spdy_framer.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_platform",
        ":spdy_core_alt_svc_wire_format_lib",
        ":spdy_core_frame_reader_lib",
        ":spdy_core_header_block_lib",
        ":spdy_core_headers_handler_interface_lib",
        ":spdy_core_hpack_hpack_lib",
        ":spdy_core_protocol_lib",
        ":spdy_core_zero_copy_output_buffer_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_frame_reader_lib",
    srcs = ["quiche/spdy/core/spdy_frame_reader.cc"],
    hdrs = ["quiche/spdy/core/spdy_frame_reader.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":spdy_core_protocol_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_header_block_lib",
    srcs = ["quiche/spdy/core/spdy_header_block.cc"],
    hdrs = ["quiche/spdy/core/spdy_header_block.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":spdy_platform",
        ":spdy_platform_unsafe_arena_lib",
    ],
)

envoy_cc_library(
    name = "spdy_core_headers_handler_interface_lib",
    hdrs = ["quiche/spdy/core/spdy_headers_handler_interface.h"],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [":spdy_platform"],
)

envoy_cc_library(
    name = "spdy_core_http2_deframer_lib",
    srcs = ["quiche/spdy/core/http2_frame_decoder_adapter.cc"],
    hdrs = ["quiche/spdy/core/http2_frame_decoder_adapter.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_constants_lib",
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_decoder_frame_decoder_lib",
        ":http2_decoder_frame_decoder_listener_lib",
        ":http2_platform",
        ":http2_structures_lib",
        ":spdy_core_alt_svc_wire_format_lib",
        ":spdy_core_header_block_lib",
        ":spdy_core_headers_handler_interface_lib",
        ":spdy_core_hpack_hpack_decoder_adapter_lib",
        ":spdy_core_hpack_hpack_lib",
        ":spdy_core_protocol_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_lifo_write_scheduler_lib",
    hdrs = ["quiche/spdy/core/lifo_write_scheduler.h"],
    repository = "@envoy",
    deps = [
        ":spdy_core_write_scheduler_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_intrusive_list_lib",
    hdrs = ["quiche/spdy/core/spdy_intrusive_list.h"],
    repository = "@envoy",
)

envoy_cc_library(
    name = "spdy_core_http2_priority_write_scheduler_lib",
    hdrs = ["quiche/spdy/core/http2_priority_write_scheduler.h"],
    repository = "@envoy",
    deps = [
        ":spdy_core_intrusive_list_lib",
        ":spdy_core_protocol_lib",
        ":spdy_core_write_scheduler_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_hpack_hpack_lib",
    srcs = [
        "quiche/spdy/core/hpack/hpack_constants.cc",
        "quiche/spdy/core/hpack/hpack_encoder.cc",
        "quiche/spdy/core/hpack/hpack_entry.cc",
        "quiche/spdy/core/hpack/hpack_header_table.cc",
        "quiche/spdy/core/hpack/hpack_huffman_table.cc",
        "quiche/spdy/core/hpack/hpack_output_stream.cc",
        "quiche/spdy/core/hpack/hpack_static_table.cc",
    ],
    hdrs = [
        "quiche/spdy/core/hpack/hpack_constants.h",
        "quiche/spdy/core/hpack/hpack_encoder.h",
        "quiche/spdy/core/hpack/hpack_entry.h",
        "quiche/spdy/core/hpack/hpack_header_table.h",
        "quiche/spdy/core/hpack/hpack_huffman_table.h",
        "quiche/spdy/core/hpack/hpack_output_stream.h",
        "quiche/spdy/core/hpack/hpack_static_table.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":spdy_core_protocol_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_hpack_hpack_decoder_adapter_lib",
    srcs = ["quiche/spdy/core/hpack/hpack_decoder_adapter.cc"],
    hdrs = ["quiche/spdy/core/hpack/hpack_decoder_adapter.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":http2_hpack_decoder_hpack_decoder_lib",
        ":http2_hpack_decoder_hpack_decoder_listener_lib",
        ":http2_hpack_decoder_hpack_decoder_tables_lib",
        ":http2_hpack_hpack_constants_lib",
        ":http2_hpack_hpack_string_lib",
        ":spdy_core_header_block_lib",
        ":spdy_core_headers_handler_interface_lib",
        ":spdy_core_hpack_hpack_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_priority_write_scheduler_lib",
    srcs = ["quiche/spdy/core/priority_write_scheduler.h"],
    repository = "@envoy",
    deps = [
        ":http2_platform",
        ":spdy_core_protocol_lib",
        ":spdy_core_write_scheduler_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_protocol_lib",
    srcs = ["quiche/spdy/core/spdy_protocol.cc"],
    hdrs = [
        "quiche/spdy/core/spdy_bitmasks.h",
        "quiche/spdy/core/spdy_protocol.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":spdy_core_alt_svc_wire_format_lib",
        ":spdy_core_header_block_lib",
        ":spdy_platform",
    ],
)

envoy_cc_library(
    name = "spdy_core_write_scheduler_lib",
    hdrs = ["quiche/spdy/core/write_scheduler.h"],
    repository = "@envoy",
    deps = [
        ":spdy_core_protocol_lib",
        ":spdy_platform",
    ],
)

envoy_cc_test_library(
    name = "spdy_core_test_utils_lib",
    srcs = ["quiche/spdy/core/spdy_test_utils.cc"],
    hdrs = ["quiche/spdy/core/spdy_test_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [
        ":spdy_core_header_block_lib",
        ":spdy_core_headers_handler_interface_lib",
        ":spdy_core_protocol_lib",
        ":spdy_platform",
        ":spdy_platform_test",
    ],
)

envoy_cc_library(
    name = "spdy_core_zero_copy_output_buffer_lib",
    hdrs = ["quiche/spdy/core/zero_copy_output_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
)

envoy_cc_library(
    name = "quic_platform",
    srcs = [
        "quiche/quic/platform/api/quic_clock.cc",
        "quiche/quic/platform/api/quic_file_utils.cc",
        "quiche/quic/platform/api/quic_hostname_utils.cc",
        "quiche/quic/platform/api/quic_mutex.cc",
    ],
    hdrs = [
        "quiche/quic/platform/api/quic_cert_utils.h",
        "quiche/quic/platform/api/quic_clock.h",
        "quiche/quic/platform/api/quic_file_utils.h",
        "quiche/quic/platform/api/quic_hostname_utils.h",
        "quiche/quic/platform/api/quic_mutex.h",
        "quiche/quic/platform/api/quic_pcc_sender.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_time_lib",
        ":quic_platform_base",
        "@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_impl_lib",
    ],
)

envoy_cc_library(
    name = "quic_platform_base",
    hdrs = [
        "quiche/quic/platform/api/quic_aligned.h",
        "quiche/quic/platform/api/quic_arraysize.h",
        "quiche/quic/platform/api/quic_bug_tracker.h",
        "quiche/quic/platform/api/quic_client_stats.h",
        "quiche/quic/platform/api/quic_containers.h",
        "quiche/quic/platform/api/quic_endian.h",
        "quiche/quic/platform/api/quic_error_code_wrappers.h",
        "quiche/quic/platform/api/quic_estimate_memory_usage.h",
        "quiche/quic/platform/api/quic_exported_stats.h",
        "quiche/quic/platform/api/quic_fallthrough.h",
        "quiche/quic/platform/api/quic_flag_utils.h",
        "quiche/quic/platform/api/quic_flags.h",
        "quiche/quic/platform/api/quic_iovec.h",
        "quiche/quic/platform/api/quic_logging.h",
        "quiche/quic/platform/api/quic_macros.h",
        "quiche/quic/platform/api/quic_map_util.h",
        "quiche/quic/platform/api/quic_mem_slice.h",
        "quiche/quic/platform/api/quic_optional.h",
        "quiche/quic/platform/api/quic_prefetch.h",
        "quiche/quic/platform/api/quic_ptr_util.h",
        "quiche/quic/platform/api/quic_reference_counted.h",
        "quiche/quic/platform/api/quic_server_stats.h",
        "quiche/quic/platform/api/quic_stack_trace.h",
        "quiche/quic/platform/api/quic_str_cat.h",
        "quiche/quic/platform/api/quic_stream_buffer_allocator.h",
        "quiche/quic/platform/api/quic_string_piece.h",
        "quiche/quic/platform/api/quic_string_utils.h",
        "quiche/quic/platform/api/quic_uint128.h",
        "quiche/quic/platform/api/quic_text_utils.h",
        # TODO: uncomment the following files as implementations are added.
        # "quiche/quic/platform/api/quic_fuzzed_data_provider.h",
        # "quiche/quic/platform/api/quic_test_loopback.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_export",
        ":quiche_common_lib",
        "@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_base_impl_lib",
    ],
)

envoy_cc_library(
    name = "quic_platform_bbr2_sender",
    hdrs = ["quiche/quic/platform/api/quic_bbr2_sender.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_bbr2_sender_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_epoll_lib",
    hdrs = ["quiche/quic/platform/api/quic_epoll.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_epoll_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_expect_bug",
    hdrs = ["quiche/quic/platform/api/quic_expect_bug.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_expect_bug_impl_lib"],
)

envoy_cc_library(
    name = "quic_platform_export",
    hdrs = ["quiche/quic/platform/api/quic_export.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_export_impl_lib"],
)

envoy_cc_library(
    name = "quic_platform_ip_address_family",
    hdrs = ["quiche/quic/platform/api/quic_ip_address_family.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "quic_platform_ip_address",
    srcs = ["quiche/quic/platform/api/quic_ip_address.cc"],
    hdrs = ["quiche/quic/platform/api/quic_ip_address.h"],
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

envoy_cc_test_library(
    name = "quic_platform_mock_log",
    hdrs = ["quiche/quic/platform/api/quic_mock_log.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_mock_log_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_port_utils",
    hdrs = ["quiche/quic/platform/api/quic_port_utils.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_port_utils_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_sleep",
    hdrs = ["quiche/quic/platform/api/quic_sleep.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_sleep_impl_lib"],
)

envoy_cc_library(
    name = "quic_platform_socket_address",
    srcs = ["quiche/quic/platform/api/quic_socket_address.cc"],
    hdrs = ["quiche/quic/platform/api/quic_socket_address.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
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
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_test_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_test_output",
    hdrs = ["quiche/quic/platform/api/quic_test_output.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_test_output_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_system_event_loop",
    hdrs = ["quiche/quic/platform/api/quic_system_event_loop.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_system_event_loop_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_thread",
    hdrs = ["quiche/quic/platform/api/quic_thread.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_thread_impl_lib"],
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

envoy_cc_library(
    name = "quic_core_alarm_interface_lib",
    srcs = ["quiche/quic/core/quic_alarm.cc"],
    hdrs = ["quiche/quic/core/quic_alarm.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_arena_scoped_ptr_lib",
        ":quic_core_time_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_alarm_factory_interface_lib",
    hdrs = ["quiche/quic/core/quic_alarm_factory.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_alarm_interface_lib",
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
    name = "quic_core_blocked_writer_interface_lib",
    hdrs = ["quiche/quic/core/quic_blocked_writer_interface.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_platform_export"],
)

envoy_cc_library(
    name = "quic_core_arena_scoped_ptr_lib",
    hdrs = ["quiche/quic/core/quic_arena_scoped_ptr.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quic_platform_base"],
)

envoy_cc_library(
    name = "quic_core_buffer_allocator_lib",
    srcs = [
        "quiche/quic/core/quic_buffer_allocator.cc",
        "quiche/quic/core/quic_simple_buffer_allocator.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_buffer_allocator.h",
        "quiche/quic/core/quic_simple_buffer_allocator.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quic_platform_export"],
)

envoy_cc_library(
    name = "quic_core_config_lib",
    srcs = ["quiche/quic/core/quic_config.cc"],
    hdrs = ["quiche/quic/core/quic_config.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
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

envoy_cc_library(
    name = "quic_core_congestion_control_bandwidth_sampler_lib",
    srcs = ["quiche/quic/core/congestion_control/bandwidth_sampler.cc"],
    hdrs = ["quiche/quic/core/congestion_control/bandwidth_sampler.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
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

envoy_cc_library(
    name = "quic_core_congestion_control_bbr_lib",
    srcs = ["quiche/quic/core/congestion_control/bbr_sender.cc"],
    hdrs = ["quiche/quic/core/congestion_control/bbr_sender.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
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

envoy_cc_library(
    name = "quic_core_congestion_control_general_loss_algorithm_lib",
    srcs = ["quiche/quic/core/congestion_control/general_loss_algorithm.cc"],
    hdrs = ["quiche/quic/core/congestion_control/general_loss_algorithm.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_congestion_control_congestion_control_interface_lib",
    hdrs = [
        "quiche/quic/core/congestion_control/loss_detection_interface.h",
        "quiche/quic/core/congestion_control/send_algorithm_interface.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_bandwidth_lib",
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

envoy_cc_library(
    name = "quic_core_congestion_control_congestion_control_lib",
    srcs = [
        "quiche/quic/core/congestion_control/send_algorithm_interface.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/loss_detection_interface.h",
        "quiche/quic/core/congestion_control/send_algorithm_interface.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_config_lib",
        ":quic_core_congestion_control_tcp_cubic_bytes_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_platform",
        ":quic_platform_bbr2_sender",
    ],
)

envoy_cc_library(
    name = "quic_core_congestion_control_pacing_sender_lib",
    srcs = ["quiche/quic/core/congestion_control/pacing_sender.cc"],
    hdrs = ["quiche/quic/core/congestion_control/pacing_sender.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_config_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_congestion_control_rtt_stats_lib",
    srcs = ["quiche/quic/core/congestion_control/rtt_stats.cc"],
    hdrs = ["quiche/quic/core/congestion_control/rtt_stats.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_congestion_control_tcp_cubic_helper",
    srcs = [
        "quiche/quic/core/congestion_control/hybrid_slow_start.cc",
        "quiche/quic/core/congestion_control/prr_sender.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/hybrid_slow_start.h",
        "quiche/quic/core/congestion_control/prr_sender.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_packets_lib",
        ":quic_core_time_lib",
        ":quic_platform_base",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_congestion_control_tcp_cubic_bytes_lib",
    srcs = [
        "quiche/quic/core/congestion_control/cubic_bytes.cc",
        "quiche/quic/core/congestion_control/tcp_cubic_sender_bytes.cc",
    ],
    hdrs = [
        "quiche/quic/core/congestion_control/cubic_bytes.h",
        "quiche/quic/core/congestion_control/tcp_cubic_sender_bytes.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
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

envoy_cc_library(
    name = "quic_core_congestion_control_uber_loss_algorithm_lib",
    srcs = ["quiche/quic/core/congestion_control/uber_loss_algorithm.cc"],
    hdrs = ["quiche/quic/core/congestion_control/uber_loss_algorithm.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_congestion_control_general_loss_algorithm_lib"],
)

envoy_cc_library(
    name = "quic_core_congestion_control_windowed_filter_lib",
    hdrs = ["quiche/quic/core/congestion_control/windowed_filter.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_time_lib"],
)

envoy_cc_library(
    name = "quic_core_connection_lib",
    srcs = ["quiche/quic/core/quic_connection.cc"],
    hdrs = ["quiche/quic/core/quic_connection.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_alarm_factory_interface_lib",
        ":quic_core_alarm_interface_lib",
        ":quic_core_bandwidth_lib",
        ":quic_core_blocked_writer_interface_lib",
        ":quic_core_config_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_one_block_arena_lib",
        ":quic_core_packet_creator_lib",
        ":quic_core_packet_generator_lib",
        ":quic_core_packet_writer_interface_lib",
        ":quic_core_packets_lib",
        ":quic_core_pending_retransmission_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_sent_packet_manager_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_uber_received_packet_manager_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_connection_stats_lib",
    srcs = ["quiche/quic/core/quic_connection_stats.cc"],
    hdrs = ["quiche/quic/core/quic_connection_stats.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_bandwidth_lib",
        ":quic_core_packets_lib",
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
    name = "quic_core_crypto_crypto_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/cert_compressor.cc",
        "quiche/quic/core/crypto/channel_id.cc",
        "quiche/quic/core/crypto/common_cert_set.cc",
        "quiche/quic/core/crypto/crypto_framer.cc",
        "quiche/quic/core/crypto/crypto_handshake.cc",
        "quiche/quic/core/crypto/crypto_handshake_message.cc",
        "quiche/quic/core/crypto/crypto_secret_boxer.cc",
        "quiche/quic/core/crypto/crypto_utils.cc",
        "quiche/quic/core/crypto/curve25519_key_exchange.cc",
        "quiche/quic/core/crypto/key_exchange.cc",
        "quiche/quic/core/crypto/p256_key_exchange.cc",
        "quiche/quic/core/crypto/quic_compressed_certs_cache.cc",
        "quiche/quic/core/crypto/quic_crypto_client_config.cc",
        "quiche/quic/core/crypto/quic_crypto_server_config.cc",
        "quiche/quic/core/crypto/transport_parameters.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/cert_compressor.h",
        "quiche/quic/core/crypto/channel_id.h",
        "quiche/quic/core/crypto/common_cert_set.h",
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
        "quiche/quic/core/crypto/quic_crypto_client_config.h",
        "quiche/quic/core/crypto/quic_crypto_server_config.h",
        "quiche/quic/core/crypto/transport_parameters.h",
    ],
    copts = quiche_copts,
    external_deps = [
        "ssl",
        "zlib",
    ],
    repository = "@envoy",
    tags = [
        "nofips",
        "pg3",
    ],
    textual_hdrs = [
        "quiche/quic/core/crypto/common_cert_set_2.c",
        "quiche/quic/core/crypto/common_cert_set_2a.inc",
        "quiche/quic/core/crypto/common_cert_set_2b.inc",
        "quiche/quic/core/crypto/common_cert_set_3.c",
        "quiche/quic/core/crypto/common_cert_set_3a.inc",
        "quiche/quic/core/crypto/common_cert_set_3b.inc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_hkdf_lib",
        ":quic_core_crypto_proof_source_interface_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_crypto_tls_handshake_lib",
        ":quic_core_data_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_lru_cache_lib",
        ":quic_core_packets_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_proto_crypto_server_config_proto_header",
        ":quic_core_proto_source_address_token_proto_header",
        ":quic_core_server_id_lib",
        ":quic_core_socket_address_coder_lib",
        ":quic_core_time_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform",
    ],
)

envoy_cc_library(
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
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_crypto_hkdf_lib",
        ":quic_core_data_lib",
        ":quic_core_packets_lib",
        ":quic_core_tag_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_crypto_hkdf_lib",
    srcs = ["quiche/quic/core/crypto/quic_hkdf.cc"],
    hdrs = ["quiche/quic/core/crypto/quic_hkdf.h"],
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_crypto_proof_source_interface_lib",
    srcs = [
        "quiche/quic/core/crypto/proof_source.cc",
        "quiche/quic/core/crypto/quic_crypto_proof.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/proof_source.h",
        "quiche/quic/core/crypto/quic_crypto_proof.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_crypto_random_lib",
    srcs = ["quiche/quic/core/crypto/quic_random.cc"],
    hdrs = ["quiche/quic/core/crypto/quic_random.h"],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quic_platform_base"],
)

envoy_cc_library(
    name = "quic_core_crypto_tls_handshake_lib",
    srcs = [
        "quiche/quic/core/crypto/tls_client_connection.cc",
        "quiche/quic/core/crypto/tls_connection.cc",
        "quiche/quic/core/crypto/tls_server_connection.cc",
    ],
    hdrs = [
        "quiche/quic/core/crypto/tls_client_connection.h",
        "quiche/quic/core/crypto/tls_connection.h",
        "quiche/quic/core/crypto/tls_server_connection.h",
    ],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_data_lib",
    srcs = [
        "quiche/quic/core/quic_data_reader.cc",
        "quiche/quic/core/quic_data_writer.cc",
    ],
    hdrs = [
        "quiche/quic/core/quic_data_reader.h",
        "quiche/quic/core/quic_data_writer.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
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
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quic_platform_export"],
)

envoy_cc_library(
    name = "quic_core_framer_lib",
    srcs = ["quiche/quic/core/quic_framer.cc"],
    hdrs = ["quiche/quic/core/quic_framer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
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
    ],
)

envoy_cc_library(
    name = "quic_core_frames_frames_lib",
    srcs = [
        "quiche/quic/core/frames/quic_ack_frame.cc",
        "quiche/quic/core/frames/quic_blocked_frame.cc",
        "quiche/quic/core/frames/quic_connection_close_frame.cc",
        "quiche/quic/core/frames/quic_crypto_frame.cc",
        "quiche/quic/core/frames/quic_frame.cc",
        "quiche/quic/core/frames/quic_goaway_frame.cc",
        "quiche/quic/core/frames/quic_max_streams_frame.cc",
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
        "quiche/quic/core/frames/quic_streams_blocked_frame.cc",
        "quiche/quic/core/frames/quic_window_update_frame.cc",
    ],
    hdrs = [
        "quiche/quic/core/frames/quic_ack_frame.h",
        "quiche/quic/core/frames/quic_blocked_frame.h",
        "quiche/quic/core/frames/quic_connection_close_frame.h",
        "quiche/quic/core/frames/quic_crypto_frame.h",
        "quiche/quic/core/frames/quic_frame.h",
        "quiche/quic/core/frames/quic_goaway_frame.h",
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
        "quiche/quic/core/frames/quic_retire_connection_id_frame.h",
        "quiche/quic/core/frames/quic_rst_stream_frame.h",
        "quiche/quic/core/frames/quic_stop_sending_frame.h",
        "quiche/quic/core/frames/quic_stop_waiting_frame.h",
        "quiche/quic/core/frames/quic_stream_frame.h",
        "quiche/quic/core/frames/quic_streams_blocked_frame.h",
        "quiche/quic/core/frames/quic_window_update_frame.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_buffer_allocator_lib",
        ":quic_core_constants_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_interval_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
        ":quic_platform_mem_slice_span",
    ],
)

envoy_cc_library(
    name = "quic_core_http_http_constants_lib",
    hdrs = ["quiche/quic/core/http/http_constants.h"],
    copts = quiche_copts,
    repository = "@envoy",
    deps = [":quic_core_types_lib"],
)

envoy_cc_library(
    name = "quic_core_http_client_lib",
    srcs = [
        "quiche/quic/core/http/quic_client_promised_info.cc",
        "quiche/quic/core/http/quic_client_push_promise_index.cc",
        "quiche/quic/core/http/quic_spdy_client_session.cc",
        "quiche/quic/core/http/quic_spdy_client_session_base.cc",
        "quiche/quic/core/http/quic_spdy_client_stream.cc",
    ],
    hdrs = [
        "quiche/quic/core/http/quic_client_promised_info.h",
        "quiche/quic/core/http/quic_client_push_promise_index.h",
        "quiche/quic/core/http/quic_spdy_client_session.h",
        "quiche/quic/core/http/quic_spdy_client_session_base.h",
        "quiche/quic/core/http/quic_spdy_client_stream.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_alarm_interface_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_http_spdy_session_lib",
        ":quic_core_packets_lib",
        ":quic_core_server_id_lib",
        ":quic_core_session_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":spdy_core_framer_lib",
        ":spdy_core_protocol_lib",
        "@envoy//source/extensions/quic_listeners/quiche:spdy_server_push_utils_for_envoy_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_http_header_list_lib",
    srcs = ["quiche/quic/core/http/quic_header_list.cc"],
    hdrs = ["quiche/quic/core/http/quic_header_list.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_platform_base",
        ":spdy_core_header_block_lib",
        ":spdy_core_headers_handler_interface_lib",
        ":spdy_core_protocol_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_http_http_decoder_lib",
    srcs = ["quiche/quic/core/http/http_decoder.cc"],
    hdrs = ["quiche/quic/core/http/http_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_data_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_http_http_frames_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_http_http_encoder_lib",
    srcs = ["quiche/quic/core/http/http_encoder.cc"],
    hdrs = ["quiche/quic/core/http/http_encoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_data_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_http_http_frames_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_http_http_frames_lib",
    hdrs = ["quiche/quic/core/http/http_frames.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_base",
        ":spdy_core_framer_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_http_spdy_server_push_utils_header",
    hdrs = ["quiche/quic/core/http/spdy_server_push_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_base",
        ":spdy_core_header_block_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_http_spdy_session_lib",
    srcs = [
        "quiche/quic/core/http/quic_headers_stream.cc",
        "quiche/quic/core/http/quic_receive_control_stream.cc",
        "quiche/quic/core/http/quic_send_control_stream.cc",
        "quiche/quic/core/http/quic_server_session_base.cc",
        "quiche/quic/core/http/quic_spdy_server_stream_base.cc",
        "quiche/quic/core/http/quic_spdy_session.cc",
        "quiche/quic/core/http/quic_spdy_stream.cc",
    ],
    hdrs = [
        "quiche/quic/core/http/quic_headers_stream.h",
        "quiche/quic/core/http/quic_receive_control_stream.h",
        "quiche/quic/core/http/quic_send_control_stream.h",
        "quiche/quic/core/http/quic_server_session_base.h",
        "quiche/quic/core/http/quic_spdy_server_stream_base.h",
        "quiche/quic/core/http/quic_spdy_session.h",
        "quiche/quic/core/http/quic_spdy_stream.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_connection_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_error_codes_lib",
        ":quic_core_http_header_list_lib",
        ":quic_core_http_http_constants_lib",
        ":quic_core_http_http_decoder_lib",
        ":quic_core_http_http_encoder_lib",
        ":quic_core_http_spdy_stream_body_buffer_lib",
        ":quic_core_http_spdy_utils_lib",
        ":quic_core_packets_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_qpack_qpack_decoded_headers_accumulator_lib",
        ":quic_core_qpack_qpack_decoder_lib",
        ":quic_core_qpack_qpack_decoder_stream_sender_lib",
        ":quic_core_qpack_qpack_encoder_lib",
        ":quic_core_qpack_qpack_encoder_stream_sender_lib",
        ":quic_core_qpack_qpack_utils_lib",
        ":quic_core_session_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
        ":quic_platform_mem_slice_storage",
        ":spdy_core_framer_lib",
        ":spdy_core_http2_deframer_lib",
        ":spdy_core_protocol_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_http_spdy_stream_body_buffer_lib",
    srcs = ["quiche/quic/core/http/quic_spdy_stream_body_buffer.cc"],
    hdrs = ["quiche/quic/core/http/quic_spdy_stream_body_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_http_http_decoder_lib",
        ":quic_core_session_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_http_spdy_utils_lib",
    srcs = ["quiche/quic/core/http/spdy_utils.cc"],
    hdrs = ["quiche/quic/core/http/spdy_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_http_header_list_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
        ":spdy_core_framer_lib",
        ":spdy_core_protocol_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_interval_lib",
    hdrs = ["quiche/quic/core/quic_interval.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
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
    ],
)

envoy_cc_library(
    name = "quic_core_lru_cache_lib",
    hdrs = ["quiche/quic/core/quic_lru_cache.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_platform_base"],
)

envoy_cc_library(
    name = "quic_core_one_block_arena_lib",
    srcs = ["quiche/quic/core/quic_one_block_arena.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_arena_scoped_ptr_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_packet_creator_lib",
    srcs = ["quiche/quic/core/quic_packet_creator.cc"],
    hdrs = ["quiche/quic/core/quic_packet_creator.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_data_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_core_pending_retransmission_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_versions_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_packet_generator_lib",
    srcs = ["quiche/quic/core/quic_packet_generator.cc"],
    hdrs = ["quiche/quic/core/quic_packet_generator.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_crypto_random_lib",
        ":quic_core_packet_creator_lib",
        ":quic_core_pending_retransmission_lib",
        ":quic_core_sent_packet_manager_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quic_platform_mem_slice_span",
    ],
)

envoy_cc_library(
    name = "quic_core_packet_number_indexed_queue_lib",
    hdrs = ["quiche/quic/core/packet_number_indexed_queue.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_packet_writer_interface_lib",
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
    deps = [
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
        ":spdy_core_fifo_write_scheduler_lib",
        ":spdy_core_http2_priority_write_scheduler_lib",
        ":spdy_core_lifo_write_scheduler_lib",
        ":spdy_core_priority_write_scheduler_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_pending_retransmission_lib",
    hdrs = ["quiche/quic/core/quic_pending_retransmission.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_frames_frames_lib",
        ":quic_core_transmission_info_lib",
        ":quic_core_types_lib",
        ":quic_platform_export",
    ],
)

envoy_cc_library(
    name = "quic_core_process_packet_interface_lib",
    hdrs = ["quiche/quic/core/quic_process_packet_interface.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_blocking_manager_lib",
    srcs = ["quiche/quic/core/qpack/qpack_blocking_manager.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_blocking_manager.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_constants_lib",
    srcs = ["quiche/quic/core/qpack/qpack_constants.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_constants.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_platform_base"],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_decoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_qpack_qpack_decoder_stream_sender_lib",
        ":quic_core_qpack_qpack_encoder_stream_receiver_lib",
        ":quic_core_qpack_qpack_header_table_lib",
        ":quic_core_qpack_qpack_progressive_decoder_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_encoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_encoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_encoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_qpack_blocking_manager_lib",
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_core_qpack_qpack_decoder_stream_receiver_lib",
        ":quic_core_qpack_qpack_encoder_stream_sender_lib",
        ":quic_core_qpack_qpack_header_table_lib",
        ":quic_core_qpack_qpack_instruction_encoder_lib",
        ":quic_core_qpack_qpack_required_insert_count_lib",
        ":quic_core_qpack_value_splitting_header_list_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_header_table_lib",
    srcs = ["quiche/quic/core/qpack/qpack_header_table.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_header_table.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_qpack_qpack_static_table_lib",
        ":quic_platform_base",
        ":spdy_core_hpack_hpack_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_instruction_decoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_instruction_decoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_instruction_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":http2_hpack_huffman_hpack_huffman_decoder_lib",
        ":http2_hpack_varint_hpack_varint_decoder_lib",
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_instruction_encoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_instruction_encoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_instruction_encoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":http2_hpack_huffman_hpack_huffman_encoder_lib",
        ":http2_hpack_varint_hpack_varint_encoder_lib",
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_platform",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_progressive_decoder_lib",
    srcs = ["quiche/quic/core/qpack/qpack_progressive_decoder.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_progressive_decoder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_core_qpack_qpack_decoder_stream_sender_lib",
        ":quic_core_qpack_qpack_encoder_stream_receiver_lib",
        ":quic_core_qpack_qpack_header_table_lib",
        ":quic_core_qpack_qpack_instruction_decoder_lib",
        ":quic_core_qpack_qpack_required_insert_count_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_required_insert_count_lib",
    srcs = ["quiche/quic/core/qpack/qpack_required_insert_count.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_required_insert_count.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_platform_base"],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_utils_lib",
    hdrs = ["quiche/quic/core/qpack/qpack_utils.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_qpack_qpack_stream_sender_delegate_lib"],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_encoder_stream_sender_lib",
    srcs = ["quiche/quic/core/qpack/qpack_encoder_stream_sender.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_encoder_stream_sender.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_core_qpack_qpack_instruction_encoder_lib",
        ":quic_core_qpack_qpack_stream_sender_delegate_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_encoder_stream_receiver_lib",
    srcs = ["quiche/quic/core/qpack/qpack_encoder_stream_receiver.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_encoder_stream_receiver.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_core_qpack_qpack_instruction_decoder_lib",
        ":quic_core_qpack_qpack_stream_receiver_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_decoder_stream_sender_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoder_stream_sender.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoder_stream_sender.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_core_qpack_qpack_instruction_encoder_lib",
        ":quic_core_qpack_qpack_stream_sender_delegate_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_decoder_stream_receiver_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoder_stream_receiver.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoder_stream_receiver.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":http2_decoder_decode_buffer_lib",
        ":http2_decoder_decode_status_lib",
        ":quic_core_qpack_qpack_constants_lib",
        ":quic_core_qpack_qpack_instruction_decoder_lib",
        ":quic_core_qpack_qpack_stream_receiver_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_static_table_lib",
    srcs = ["quiche/quic/core/qpack/qpack_static_table.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_static_table.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_platform_base",
        ":spdy_core_hpack_hpack_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_stream_receiver_lib",
    hdrs = ["quiche/quic/core/qpack/qpack_stream_receiver.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_platform_base"],
)

envoy_cc_library(
    name = "quic_core_qpack_qpack_decoded_headers_accumulator_lib",
    srcs = ["quiche/quic/core/qpack/qpack_decoded_headers_accumulator.cc"],
    hdrs = ["quiche/quic/core/qpack/qpack_decoded_headers_accumulator.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_http_header_list_lib",
        ":quic_core_qpack_qpack_decoder_lib",
        ":quic_core_qpack_qpack_progressive_decoder_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_qpack_value_splitting_header_list_lib",
    srcs = ["quiche/quic/core/qpack/value_splitting_header_list.cc"],
    hdrs = ["quiche/quic/core/qpack/value_splitting_header_list.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_platform_base",
        ":spdy_core_header_block_lib",
    ],
)

# envoy_cc_library(
#     name = "quic_core_qpack_qpack_streams_lib",
#     srcs = [
#         "quiche/quic/core/qpack/qpack_receive_stream.cc",
#         "quiche/quic/core/qpack/qpack_send_stream.cc",
#     ],
#     hdrs = [
#         "quiche/quic/core/qpack/qpack_receive_stream.h",
#         "quiche/quic/core/qpack/qpack_send_stream.h",
#     ],
#     copts = quiche_copts,
#     repository = "@envoy",
#     deps = [
#         ":quic_core_http_spdy_session_lib",
#         ":quic_core_qpack_qpack_stream_sender_delegate_lib",
#         ":quic_core_session_lib",
#         ":quic_platform_base",
#     ],
# )

envoy_cc_library(
    name = "quic_core_qpack_qpack_stream_sender_delegate_lib",
    hdrs = ["quiche/quic/core/qpack/qpack_stream_sender_delegate.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_platform_base"],
)

envoy_cc_library(
    name = "quic_core_received_packet_manager_lib",
    srcs = ["quiche/quic/core/quic_received_packet_manager.cc"],
    hdrs = ["quiche/quic/core/quic_received_packet_manager.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
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

envoy_cc_library(
    name = "quic_core_sent_packet_manager_lib",
    srcs = ["quiche/quic/core/quic_sent_packet_manager.cc"],
    hdrs = ["quiche/quic/core/quic_sent_packet_manager.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_congestion_control_congestion_control_lib",
        ":quic_core_congestion_control_general_loss_algorithm_lib",
        ":quic_core_congestion_control_pacing_sender_lib",
        ":quic_core_congestion_control_rtt_stats_lib",
        ":quic_core_congestion_control_uber_loss_algorithm_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_packets_lib",
        ":quic_core_pending_retransmission_lib",
        ":quic_core_proto_cached_network_parameters_proto_header",
        ":quic_core_sustained_bandwidth_recorder_lib",
        ":quic_core_transmission_info_lib",
        ":quic_core_types_lib",
        ":quic_core_unacked_packet_map_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_server_id_lib",
    srcs = ["quiche/quic/core/quic_server_id.cc"],
    hdrs = ["quiche/quic/core/quic_server_id.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_server_lib",
    srcs = [
        "quiche/quic/core/chlo_extractor.cc",
        "quiche/quic/core/quic_buffered_packet_store.cc",
        "quiche/quic/core/quic_dispatcher.cc",
    ],
    hdrs = [
        "quiche/quic/core/chlo_extractor.h",
        "quiche/quic/core/quic_buffered_packet_store.h",
        "quiche/quic/core/quic_dispatcher.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_alarm_factory_interface_lib",
        ":quic_core_alarm_interface_lib",
        ":quic_core_blocked_writer_interface_lib",
        ":quic_core_connection_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_core_process_packet_interface_lib",
        ":quic_core_session_lib",
        ":quic_core_time_lib",
        ":quic_core_time_wait_list_manager_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_core_version_manager_lib",
        ":quic_platform",
    ],
)

envoy_cc_library(
    name = "quic_core_session_lib",
    srcs = [
        "quiche/quic/core/legacy_quic_stream_id_manager.cc",
        "quiche/quic/core/quic_control_frame_manager.cc",
        "quiche/quic/core/quic_crypto_client_handshaker.cc",
        "quiche/quic/core/quic_crypto_client_stream.cc",
        "quiche/quic/core/quic_crypto_handshaker.cc",
        "quiche/quic/core/quic_crypto_server_handshaker.cc",
        "quiche/quic/core/quic_crypto_server_stream.cc",
        "quiche/quic/core/quic_crypto_stream.cc",
        "quiche/quic/core/quic_flow_controller.cc",
        "quiche/quic/core/quic_session.cc",
        "quiche/quic/core/quic_stream.cc",
        "quiche/quic/core/quic_stream_id_manager.cc",
        "quiche/quic/core/quic_stream_sequencer.cc",
        "quiche/quic/core/tls_client_handshaker.cc",
        "quiche/quic/core/tls_handshaker.cc",
        "quiche/quic/core/tls_server_handshaker.cc",
        "quiche/quic/core/uber_quic_stream_id_manager.cc",
    ],
    hdrs = [
        "quiche/quic/core/legacy_quic_stream_id_manager.h",
        "quiche/quic/core/quic_control_frame_manager.h",
        "quiche/quic/core/quic_crypto_client_handshaker.h",
        "quiche/quic/core/quic_crypto_client_stream.h",
        "quiche/quic/core/quic_crypto_handshaker.h",
        "quiche/quic/core/quic_crypto_server_handshaker.h",
        "quiche/quic/core/quic_crypto_server_stream.h",
        "quiche/quic/core/quic_crypto_stream.h",
        "quiche/quic/core/quic_flow_controller.h",
        "quiche/quic/core/quic_session.h",
        "quiche/quic/core/quic_stream.h",
        "quiche/quic/core/quic_stream_id_manager.h",
        "quiche/quic/core/quic_stream_sequencer.h",
        "quiche/quic/core/tls_client_handshaker.h",
        "quiche/quic/core/tls_handshaker.h",
        "quiche/quic/core/tls_server_handshaker.h",
        "quiche/quic/core/uber_quic_stream_id_manager.h",
    ],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_config_lib",
        ":quic_core_connection_lib",
        ":quic_core_constants_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_crypto_tls_handshake_lib",
        ":quic_core_frames_frames_lib",
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
        ":quic_platform_mem_slice_span",
        ":spdy_core_protocol_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_session_notifier_interface_lib",
    hdrs = ["quiche/quic/core/session_notifier_interface.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_frames_frames_lib",
        ":quic_core_time_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_socket_address_coder_lib",
    srcs = ["quiche/quic/core/quic_socket_address_coder.cc"],
    hdrs = ["quiche/quic/core/quic_socket_address_coder.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_platform_base",
        ":quic_platform_socket_address",
        ":spdy_core_priority_write_scheduler_lib",
    ],
)

envoy_cc_library(
    name = "quic_core_stream_frame_data_producer_lib",
    hdrs = ["quiche/quic/core/quic_stream_frame_data_producer.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_types_lib"],
)

envoy_cc_library(
    name = "quic_core_stream_send_buffer_lib",
    srcs = ["quiche/quic/core/quic_stream_send_buffer.cc"],
    hdrs = ["quiche/quic/core/quic_stream_send_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_core_data_lib",
        ":quic_core_frames_frames_lib",
        ":quic_core_interval_lib",
        ":quic_core_interval_set_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
        ":quic_platform_mem_slice_span",
    ],
)

envoy_cc_library(
    name = "quic_core_stream_sequencer_buffer_lib",
    srcs = ["quiche/quic/core/quic_stream_sequencer_buffer.cc"],
    hdrs = ["quiche/quic/core/quic_stream_sequencer_buffer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_constants_lib",
        ":quic_core_interval_lib",
        ":quic_core_interval_set_lib",
        ":quic_core_packets_lib",
        ":quic_core_types_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_sustained_bandwidth_recorder_lib",
    srcs = ["quiche/quic/core/quic_sustained_bandwidth_recorder.cc"],
    hdrs = ["quiche/quic/core/quic_sustained_bandwidth_recorder.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
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
    deps = [":quic_platform_base"],
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

envoy_cc_library(
    name = "quic_core_time_wait_list_manager_lib",
    srcs = ["quiche/quic/core/quic_time_wait_list_manager.cc"],
    hdrs = ["quiche/quic/core/quic_time_wait_list_manager.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_blocked_writer_interface_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_packet_writer_interface_lib",
        ":quic_core_packets_lib",
        ":quic_core_session_lib",
        ":quic_core_types_lib",
        ":quic_core_utils_lib",
        ":quic_platform",
    ],
)

envoy_cc_library(
    name = "quic_core_transmission_info_lib",
    srcs = ["quiche/quic/core/quic_transmission_info.cc"],
    hdrs = ["quiche/quic/core/quic_transmission_info.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
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
    ],
)

envoy_cc_library(
    name = "quic_core_uber_received_packet_manager_lib",
    srcs = ["quiche/quic/core/uber_received_packet_manager.cc"],
    hdrs = ["quiche/quic/core/uber_received_packet_manager.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_received_packet_manager_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_core_unacked_packet_map_lib",
    srcs = ["quiche/quic/core/quic_unacked_packet_map.cc"],
    hdrs = ["quiche/quic/core/quic_unacked_packet_map.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_connection_stats_lib",
        ":quic_core_packets_lib",
        ":quic_core_session_notifier_interface_lib",
        ":quic_core_transmission_info_lib",
        ":quic_core_utils_lib",
        ":quic_platform_base",
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
    ],
)

envoy_cc_library(
    name = "quic_core_version_manager_lib",
    srcs = ["quiche/quic/core/quic_version_manager.cc"],
    hdrs = ["quiche/quic/core/quic_version_manager.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_versions_lib",
        ":quic_platform_base",
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
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_config_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_config_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_config_peer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_config_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_framer_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_framer_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_framer_peer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_mock_clock_lib",
    srcs = ["quiche/quic/test_tools/mock_clock.cc"],
    hdrs = ["quiche/quic/test_tools/mock_clock.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_time_lib",
        ":quic_platform",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_mock_random_lib",
    srcs = ["quiche/quic/test_tools/mock_random.cc"],
    hdrs = ["quiche/quic/test_tools/mock_random.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_crypto_random_lib"],
)

envoy_cc_test_library(
    name = "quic_test_tools_packet_generator_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_packet_generator_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_packet_generator_peer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_packet_creator_lib",
        ":quic_core_packet_generator_lib",
        ":quic_core_packets_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_sent_packet_manager_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_sent_packet_manager_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_sent_packet_manager_peer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_packets_lib",
        ":quic_core_sent_packet_manager_lib",
        ":quic_test_tools_unacked_packet_map_peer_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_simple_quic_framer_lib",
    srcs = ["quiche/quic/test_tools/simple_quic_framer.cc"],
    hdrs = ["quiche/quic/test_tools/simple_quic_framer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_crypto_encryption_lib",
        ":quic_core_framer_lib",
        ":quic_core_packets_lib",
        ":quic_platform_base",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_stream_send_buffer_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_stream_send_buffer_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_stream_send_buffer_peer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_stream_send_buffer_lib"],
)

envoy_cc_test_library(
    name = "quic_test_tools_stream_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_stream_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_stream_peer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_packets_lib",
        ":quic_core_session_lib",
        ":quic_core_stream_send_buffer_lib",
        ":quic_platform_base",
        ":quic_test_tools_stream_send_buffer_peer_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_test_utils_interface_lib",
    srcs = [
        "quiche/quic/test_tools/crypto_test_utils.cc",
        "quiche/quic/test_tools/mock_quic_session_visitor.cc",
        "quiche/quic/test_tools/mock_quic_time_wait_list_manager.cc",
        "quiche/quic/test_tools/quic_connection_peer.cc",
        "quiche/quic/test_tools/quic_dispatcher_peer.cc",
        "quiche/quic/test_tools/quic_test_utils.cc",
    ],
    hdrs = [
        "quiche/quic/test_tools/crypto_test_utils.h",
        "quiche/quic/test_tools/mock_quic_session_visitor.h",
        "quiche/quic/test_tools/mock_quic_time_wait_list_manager.h",
        "quiche/quic/test_tools/quic_connection_peer.h",
        "quiche/quic/test_tools/quic_dispatcher_peer.h",
        "quiche/quic/test_tools/quic_test_utils.h",
    ],
    copts = quiche_copts,
    external_deps = ["ssl"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_buffer_allocator_lib",
        ":quic_core_congestion_control_congestion_control_interface_lib",
        ":quic_core_connection_lib",
        ":quic_core_connection_stats_lib",
        ":quic_core_crypto_crypto_handshake_lib",
        ":quic_core_crypto_encryption_lib",
        ":quic_core_crypto_proof_source_interface_lib",
        ":quic_core_crypto_random_lib",
        ":quic_core_data_lib",
        ":quic_core_framer_lib",
        ":quic_core_http_client_lib",
        ":quic_core_http_spdy_session_lib",
        ":quic_core_packet_creator_lib",
        ":quic_core_packet_writer_interface_lib",
        ":quic_core_packets_lib",
        ":quic_core_received_packet_manager_lib",
        ":quic_core_sent_packet_manager_lib",
        ":quic_core_server_id_lib",
        ":quic_core_server_lib",
        ":quic_core_session_lib",
        ":quic_core_time_wait_list_manager_lib",
        ":quic_core_utils_lib",
        ":quic_platform",
        ":quic_platform_test",
        ":quic_test_tools_config_peer_lib",
        ":quic_test_tools_framer_peer_lib",
        ":quic_test_tools_mock_clock_lib",
        ":quic_test_tools_mock_random_lib",
        ":quic_test_tools_packet_generator_peer_lib",
        ":quic_test_tools_sent_packet_manager_peer_lib",
        ":quic_test_tools_simple_quic_framer_lib",
        ":quic_test_tools_stream_peer_lib",
        ":spdy_core_framer_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_test_tools_unacked_packet_map_peer_lib",
    srcs = ["quiche/quic/test_tools/quic_unacked_packet_map_peer.cc"],
    hdrs = ["quiche/quic/test_tools/quic_unacked_packet_map_peer.h"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":quic_core_unacked_packet_map_lib"],
)

envoy_cc_test_library(
    name = "epoll_server_platform",
    hdrs = [
        "quiche/epoll_server/platform/api/epoll_address_test_utils.h",
        "quiche/epoll_server/platform/api/epoll_bug.h",
        "quiche/epoll_server/platform/api/epoll_expect_bug.h",
        "quiche/epoll_server/platform/api/epoll_export.h",
        "quiche/epoll_server/platform/api/epoll_logging.h",
        "quiche/epoll_server/platform/api/epoll_ptr_util.h",
        "quiche/epoll_server/platform/api/epoll_test.h",
        "quiche/epoll_server/platform/api/epoll_thread.h",
        "quiche/epoll_server/platform/api/epoll_time.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:epoll_server_platform_impl_lib"],
)

envoy_cc_test_library(
    name = "epoll_server_lib",
    srcs = select({
        "@envoy//bazel:linux": [
            "quiche/epoll_server/fake_simple_epoll_server.cc",
            "quiche/epoll_server/simple_epoll_server.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:linux": [
            "quiche/epoll_server/fake_simple_epoll_server.h",
            "quiche/epoll_server/simple_epoll_server.h",
        ],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":epoll_server_platform"],
)

envoy_cc_library(
    name = "quiche_common_platform",
    hdrs = [
        "quiche/common/platform/api/quiche_logging.h",
        "quiche/common/platform/api/quiche_ptr_util.h",
        "quiche/common/platform/api/quiche_unordered_containers.h",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quiche_common_platform_impl_lib"],
)

envoy_cc_test_library(
    name = "quiche_common_platform_test",
    hdrs = ["quiche/common/platform/api/quiche_test.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quiche_common_platform_test_impl_lib"],
)

envoy_cc_library(
    name = "quiche_common_lib",
    hdrs = ["quiche/common/simple_linked_hash_map.h"],
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [":quiche_common_platform"],
)

envoy_cc_test(
    name = "epoll_server_test",
    srcs = select({
        "@envoy//bazel:linux": ["quiche/epoll_server/simple_epoll_server_test.cc"],
        "//conditions:default": [],
    }),
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [":epoll_server_lib"],
)

envoy_cc_test(
    name = "quiche_common_test",
    srcs = ["quiche/common/simple_linked_hash_map_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quiche_common_lib",
        ":quiche_common_platform_test",
    ],
)

envoy_cc_test(
    name = "http2_platform_api_test",
    srcs = [
        "quiche/http2/platform/api/http2_string_utils_test.cc",
        "quiche/http2/test_tools/http2_random_test.cc",
    ],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":http2_platform",
        ":http2_test_tools_random",
    ],
)

envoy_cc_test(
    name = "spdy_platform_api_test",
    srcs = ["quiche/spdy/platform/api/spdy_string_utils_test.cc"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":spdy_platform",
        ":spdy_platform_test",
    ],
)

envoy_cc_library(
    name = "quic_platform_mem_slice_span",
    hdrs = [
        "quiche/quic/platform/api/quic_mem_slice_span.h",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_mem_slice_span_impl_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_test_mem_slice_vector_lib",
    hdrs = ["quiche/quic/platform/api/quic_test_mem_slice_vector.h"],
    repository = "@envoy",
    tags = ["nofips"],
    deps = ["@envoy//test/extensions/quic_listeners/quiche/platform:quic_platform_test_mem_slice_vector_impl_lib"],
)

envoy_cc_library(
    name = "quic_platform_mem_slice_storage",
    hdrs = ["quiche/quic/platform/api/quic_mem_slice_storage.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = ["@envoy//source/extensions/quic_listeners/quiche/platform:quic_platform_mem_slice_storage_impl_lib"],
)

envoy_cc_test(
    name = "spdy_core_header_block_test",
    srcs = ["quiche/spdy/core/spdy_header_block_test.cc"],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":spdy_core_header_block_lib",
        ":spdy_core_test_utils_lib",
    ],
)

envoy_cc_test(
    name = "quic_platform_api_test",
    srcs = [
        "quiche/quic/platform/api/quic_containers_test.cc",
        "quiche/quic/platform/api/quic_endian_test.cc",
        "quiche/quic/platform/api/quic_mem_slice_span_test.cc",
        "quiche/quic/platform/api/quic_mem_slice_storage_test.cc",
        "quiche/quic/platform/api/quic_mem_slice_test.cc",
        "quiche/quic/platform/api/quic_reference_counted_test.cc",
        "quiche/quic/platform/api/quic_string_utils_test.cc",
        "quiche/quic/platform/api/quic_text_utils_test.cc",
    ],
    copts = quiche_copts,
    repository = "@envoy",
    tags = ["nofips"],
    deps = [
        ":quic_core_buffer_allocator_lib",
        ":quic_platform",
        ":quic_platform_mem_slice_span",
        ":quic_platform_mem_slice_storage",
        ":quic_platform_test",
        ":quic_platform_test_mem_slice_vector_lib",
    ],
)

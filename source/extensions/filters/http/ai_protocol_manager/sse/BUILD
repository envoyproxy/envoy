load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "sse_scanner_lib",
    srcs = ["sse_scanner.cc"],
    hdrs = ["sse_scanner.h"],
    deps = [
        "//source/common/common:assert_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "sse_event_lib",
    srcs = ["sse_event.cc"],
    hdrs = ["sse_event.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "sse_event_codec_lib",
    srcs = ["sse_event_codec.cc"],
    hdrs = ["sse_event_codec.h"],
    deps = [
        ":sse_event_lib",
        ":sse_scanner_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/coroutine:status_macros_lib",
        "//source/common/coroutine:task_lib",
        "//source/extensions/filters/http/ai_protocol_manager:buffer_manager_lib",
        "//source/extensions/filters/http/ai_protocol_manager:external_buffer_interface",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_parser_lib",
        "//source/extensions/filters/http/ai_protocol_manager:replay_awaitable_lib",
        "//source/extensions/filters/http/ai_protocol_manager:serializer_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

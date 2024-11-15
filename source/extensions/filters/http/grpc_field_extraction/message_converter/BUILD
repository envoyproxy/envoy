load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "stream_message_lib",
    hdrs = [
        "stream_message.h",
    ],
    deps = [
        ":message_converter_utility_lib",
        "@com_google_protofieldextraction//:all_libs",
    ],
)

envoy_cc_library(
    name = "message_converter_utility_lib",
    srcs = [
        "message_converter_utility.cc",
    ],
    hdrs = [
        "message_converter_utility.h",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:logger_lib",
        "@com_google_protobuf//:protobuf",
        "@com_google_protofieldextraction//:all_libs",
        "@grpc_httpjson_transcoding//src:message_reader",
    ],
)

envoy_cc_library(
    name = "message_converter_lib",
    srcs = [
        "message_converter.cc",
    ],
    hdrs = [
        "message_converter.h",
    ],
    deps = [
        ":message_converter_utility_lib",
        ":stream_message_lib",
        "@com_google_protobuf//:protobuf",
    ],
)

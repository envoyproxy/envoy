load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "substitution_formatter_lib",
    srcs = ["substitution_formatter.cc"],
    hdrs = ["substitution_formatter.h"],
    external_deps = ["abseil_str_format"],
    deps = [
        "//include/envoy/formatter:substitution_formatter_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/stream_info:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "substitution_format_string_lib",
    srcs = ["substitution_format_string.cc"],
    hdrs = ["substitution_format_string.h"],
    deps = [
        ":substitution_formatter_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

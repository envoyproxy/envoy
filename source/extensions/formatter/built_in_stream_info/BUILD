load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["stream_info_formatter.cc"],
    hdrs = ["stream_info_formatter.h"],
    # previously considered core code.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/common:utility_lib",
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/stream_info:utility_lib",
    ],
)

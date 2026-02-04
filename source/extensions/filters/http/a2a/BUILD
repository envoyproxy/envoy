load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "a2a_json_parser_lib",
    srcs = ["a2a_json_parser.cc"],
    hdrs = ["a2a_json_parser.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/json:json_rpc_parser_lib",
        "//source/common/protobuf",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

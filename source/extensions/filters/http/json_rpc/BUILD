load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "json_rpc_decoder_lib",
    hdrs = ["json_rpc_decoder.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "json_rpc_parser_lib",
    srcs = ["json_rpc_parser.cc"],
    hdrs = ["json_rpc_parser.h"],
    deps = [
        ":json_rpc_decoder_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:logger_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "json_rpc_connection_manager_lib",
    srcs = ["json_rpc_connection_manager.cc"],
    hdrs = ["json_rpc_connection_manager.h"],
    deps = [
        ":json_rpc_decoder_lib",
        ":json_rpc_parser_lib",
        "//envoy/http:filter_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
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
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "a2a_filter_lib",
    srcs = ["a2a_filter.cc"],
    hdrs = ["a2a_filter.h"],
    deps = [
        ":a2a_json_parser_lib",
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:logger_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@envoy_api//envoy/extensions/filters/http/a2a/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":a2a_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/a2a/v3:pkg_cc_proto",
    ],
)

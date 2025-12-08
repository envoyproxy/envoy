load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "mcp_json_parser_lib",
    srcs = ["mcp_json_parser.cc"],
    hdrs = ["mcp_json_parser.h"],
    external_deps = ["grpc_transcoding"],
    deps = [
        "//source/common/common:logger_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "mcp_filter_lib",
    srcs = ["mcp_filter.cc"],
    hdrs = ["mcp_filter.h"],
    deps = [
        ":mcp_json_parser_lib",
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":mcp_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
    ],
)

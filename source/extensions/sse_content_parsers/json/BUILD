load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "json_content_parser_lib",
    srcs = ["json_content_parser_impl.cc"],
    hdrs = ["json_content_parser_impl.h"],
    deps = [
        "//envoy/sse_content_parser:factory_interface",
        "//envoy/sse_content_parser:parser_interface",
        "//source/common/json:json_loader_lib",
        "@envoy_api//envoy/extensions/sse_content_parsers/json/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":json_content_parser_lib",
        "//envoy/registry",
        "//envoy/sse_content_parser:config_interface",
        "@envoy_api//envoy/extensions/sse_content_parsers/json/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":utils_lib",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//source/common/common:statusor_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "parser_lib",
    srcs = ["parser.cc"],
    hdrs = ["parser.h"],
    deps = ["//source/common/common:statusor_lib"],
)

envoy_cc_library(
    name = "utils_lib",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    deps = [
        ":parser_lib",
        "//source/common/common:statusor_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        ":parser_lib",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/common:statusor_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/cdn_loop/v3:pkg_cc_proto",
    ],
)

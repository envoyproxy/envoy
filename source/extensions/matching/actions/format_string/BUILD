load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/http:header_map_lib",
        "//source/common/matcher:matcher_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

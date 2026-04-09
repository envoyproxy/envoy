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
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/common:utility_lib",
        "//source/common/config:datasource_lib",
        "//source/common/formatter:substitution_format_utility_lib",
        "@envoy_api//envoy/extensions/formatter/file_content/v3:pkg_cc_proto",
    ],
)

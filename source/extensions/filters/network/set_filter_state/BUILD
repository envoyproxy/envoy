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
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/common/set_filter_state:filter_config_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//envoy/extensions/filters/network/set_filter_state/v3:pkg_cc_proto",
    ],
)

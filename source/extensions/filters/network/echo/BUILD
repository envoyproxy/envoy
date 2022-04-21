load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Echo L4 network filter. This is primarily a simplistic example.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/echo_filter

envoy_extension_package()

envoy_cc_library(
    name = "echo",
    srcs = ["echo.cc"],
    hdrs = ["echo.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    # The echo filter is used in integration tests which don't need an upstream.
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    deps = [
        ":echo",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/echo/v3:pkg_cc_proto",
    ],
)

licenses(["notice"])  # Apache 2

# L7 HTTP Tap filter
# Public docs: docs/root/configuration/http_filters/tap_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tap_config_interface",
    hdrs = ["tap_config.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/http:header_map_interface",
        "//source/extensions/common/tap:tap_interface",
    ],
)

envoy_cc_library(
    name = "tap_config_impl",
    srcs = ["tap_config_impl.cc"],
    hdrs = ["tap_config_impl.h"],
    deps = [
        ":tap_config_interface",
        "//source/extensions/common/tap:tap_config_base",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tap_filter_lib",
    srcs = ["tap_filter.cc"],
    hdrs = ["tap_filter.h"],
    deps = [
        ":tap_config_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/http:filter_interface",
        "//source/extensions/common/tap:extension_config_base",
        "@envoy_api//envoy/extensions/filters/http/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":tap_config_impl",
        ":tap_filter_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/tap/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_admin_functionality",
)

licenses(["notice"])  # Apache 2

# L7 HTTP Tap filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/tap_filter

envoy_extension_package()

envoy_cc_library(
    name = "tap_config_interface",
    hdrs = ["tap_config.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/http:header_map_interface",
        "//source/extensions/common/tap:tap_interface",
        "@envoy_api//envoy/extensions/filters/http/tap/v3:pkg_cc_proto",
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
        "@envoy_api//envoy/extensions/filters/http/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tap_filter_lib",
    srcs = envoy_select_admin_functionality(["tap_filter.cc"]),
    hdrs = envoy_select_admin_functionality(["tap_filter.h"]),
    deps = [
        ":tap_config_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/http:filter_interface",
        "//source/extensions/common/tap:extension_config_base",
        "@envoy_api//envoy/extensions/filters/http/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = envoy_select_admin_functionality(["config.cc"]),
    hdrs = envoy_select_admin_functionality(["config.h"]),
    deps = [
        ":tap_config_impl",
        ":tap_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/tap/v3:pkg_cc_proto",
    ],
)

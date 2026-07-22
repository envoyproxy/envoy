load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":upstream_rbac_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_rbac_filter_lib",
    srcs = ["upstream_rbac_filter.cc"],
    hdrs = ["upstream_rbac_filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:header_map_lib",
        "//source/common/stream_info:upstream_address_lib",
        "//source/extensions/filters/common/rbac:engine_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
    ],
)

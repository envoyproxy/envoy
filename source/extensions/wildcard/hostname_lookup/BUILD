load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "hostname_lookup_lib",
    srcs = ["hostname_lookup.cc"],
    hdrs = ["hostname_lookup.h"],
    deps = [
        "//envoy/network:filter_interface",
        "//source/common/common:logger_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:utility_lib",
        "//source/extensions/wildcard/virtual_ip_cache:cache_manager_lib",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":hostname_lookup_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/wildcard/virtual_ip_cache:cache_manager_lib",
        "@envoy_api//envoy/extensions/wildcard/hostname_lookup/v3:pkg_cc_proto",
    ],
)

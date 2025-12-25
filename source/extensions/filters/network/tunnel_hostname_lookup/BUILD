load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "tunnel_hostname_lookup_lib",
    srcs = ["tunnel_hostname_lookup.cc"],
    hdrs = ["tunnel_hostname_lookup.h"],
    deps = [
        "//envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:utility_lib",
        "//source/extensions/common/synthetic_ip:cache_manager_lib",
        "@envoy_api//envoy/extensions/filters/network/tunnel_hostname_lookup/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":tunnel_hostname_lookup_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/common/synthetic_ip:cache_manager_lib",
        "@envoy_api//envoy/extensions/filters/network/tunnel_hostname_lookup/v3:pkg_cc_proto",
    ],
)

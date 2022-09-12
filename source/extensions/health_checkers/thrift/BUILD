load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Thrift custom health checker.

envoy_extension_package()

envoy_cc_library(
    name = "client_interface",
    hdrs = ["client.h"],
    deps = [
        "//envoy/upstream:cluster_manager_interface",
        "//source/extensions/filters/network/thrift_proxy:thrift_lib",
    ],
)

envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
        ":client_interface",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy:passthrough_decoder_event_handler_lib",
        "//source/extensions/filters/network/thrift_proxy:thrift_lib",
    ],
)

envoy_cc_library(
    name = "thrift",
    srcs = ["thrift.cc"],
    hdrs = ["thrift.h"],
    deps = [
        ":client_lib",
        "//source/common/upstream:health_checker_base_lib",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy:thrift_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/thrift/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":client_lib",
        ":thrift",
        ":utility",
        "//envoy/registry",
        "//envoy/server:health_checker_config_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/thrift/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/thrift/v3:pkg_cc_proto",
    ],
)

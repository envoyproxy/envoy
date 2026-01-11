load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_config_lib",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/config:utility_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/filters/udp/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = [
        "abi_impl.cc",
        "filter.cc",
    ],
    hdrs = [
        "abi_impl.h",
        "filter.h",
    ],
    deps = [
        ":filter_config_lib",
        "//envoy/network:filter_interface",
        "//envoy/network:listener_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/stats:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["factory.cc"],
    hdrs = ["factory.h"],
    deps = [
        ":filter_config_lib",
        ":filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
    ],
)

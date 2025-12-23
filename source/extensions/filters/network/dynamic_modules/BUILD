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
        "//source/common/config:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = [
        "abi_impl.cc",
        "filter.cc",
    ],
    hdrs = ["filter.h"],
    deps = [
        ":filter_config_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
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
        "@envoy_api//envoy/extensions/filters/network/dynamic_modules/v3:pkg_cc_proto",
    ],
)

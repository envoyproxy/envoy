load(
    "//bazel:envoy_build_system.bzl",
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
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":filter_config_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "factory_lib",
    srcs = ["factory.cc"],
    hdrs = ["factory.h"],
    deps = [
        ":abi_impl",
        ":filter_config_lib",
        ":filter_lib",
        "@envoy_api//envoy/extensions/filters/http/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "factory_registration",
    srcs = ["factory_registration.cc"],
    deps = [
        ":factory_lib",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":filter_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

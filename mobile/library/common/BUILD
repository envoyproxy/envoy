load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "envoy_main_interface_lib",
    repository = "@envoy",
    deps = [
        ":envoy_main_interface_lib_no_stamp",
        ":envoy_mobile_main_common_lib_stamped",
    ],
)

envoy_cc_library(
    name = "envoy_main_interface_lib_no_stamp",
    srcs = [
        "certificates.inc",
        "config_template.cc",
        "engine.cc",
        "engine.h",
        "main_interface.cc",
    ],
    hdrs = ["main_interface.h"],
    repository = "@envoy",
    deps = [
        ":envoy_mobile_main_common_lib",
        "//library/common/data:utility_lib",
        "//library/common/http:dispatcher_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//include/envoy/server:lifecycle_notifier_interface",
        "@envoy_build_config//:extension_registry",
    ],
)

envoy_cc_library(
    name = "envoy_mobile_main_common_lib",
    srcs = ["envoy_mobile_main_common.cc"],
    hdrs = ["envoy_mobile_main_common.h"],
    repository = "@envoy",
    deps = [
        "@envoy//source/common/common:random_generator_lib",
        "@envoy//source/common/runtime:runtime_lib",
        "@envoy//source/exe:main_common_lib",
    ],
)

envoy_cc_library(
    name = "envoy_mobile_main_common_lib_stamped",
    repository = "@envoy",
    deps = [
        ":envoy_mobile_main_common_lib",
        "@envoy//source/exe:envoy_main_common_lib",
    ],
)

licenses(["notice"])  # Apache 2

# Find and Replace network filter

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "find_and_replace",
    srcs = ["find_and_replace.cc"],
    hdrs = ["find_and_replace.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "@envoy_api//envoy/config/filter/network/find_and_replace/v2alpha1:find_and_replace_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":find_and_replace",
        "//include/envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/network/find_and_replace/v2alpha1:find_and_replace_cc",
    ],
)

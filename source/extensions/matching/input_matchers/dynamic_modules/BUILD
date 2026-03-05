load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "matcher_lib",
    srcs = [
        "abi_impl.cc",
        "matcher.cc",
    ],
    hdrs = ["matcher.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:header_utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "//source/extensions/matching/http/dynamic_modules:data_input_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":matcher_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/matching/input_matchers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

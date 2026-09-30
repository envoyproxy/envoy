load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "config_validator_lib",
    srcs = [
        "abi_impl.cc",
        "config_validator.cc",
    ],
    hdrs = ["config_validator.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/config:config_validator_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:statusor_lib",
        "//source/common/common:thread_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":config_validator_lib",
        "//envoy/common:exception_lib",
        "//envoy/config:config_validator_interface",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/config/validators/dynamic_modules/v3:pkg_cc_proto",
    ],
)

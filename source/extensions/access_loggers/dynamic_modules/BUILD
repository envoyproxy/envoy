load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "access_log_config_lib",
    srcs = ["access_log_config.cc"],
    hdrs = ["access_log_config.h"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/access_loggers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "access_log_lib",
    srcs = [
        "abi_impl.cc",
        "access_log.cc",
    ],
    hdrs = [
        "access_log.h",
    ],
    deps = [
        ":access_log_config_lib",
        "//envoy/access_log:access_log_interface",
        "//source/common/config:metadata_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":access_log_config_lib",
        ":access_log_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/access_loggers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Stats sink that delegates flush and histogram observations to a dynamic
# module loaded via dlopen (see source/extensions/dynamic_modules/abi).

envoy_extension_package()

envoy_cc_library(
    name = "sink_config_lib",
    srcs = ["sink_config.cc"],
    hdrs = ["sink_config.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/stat_sinks/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "sink_lib",
    srcs = ["sink.cc"],
    hdrs = ["sink.h"],
    deps = [
        ":sink_config_lib",
        "//envoy/stats:sink_interface",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":sink_config_lib",
        ":sink_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/extensions/stat_sinks/dynamic_modules/v3:pkg_cc_proto",
    ],
)

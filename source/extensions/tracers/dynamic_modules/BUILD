load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "tracer_config_lib",
    srcs = ["tracer_config.cc"],
    hdrs = ["tracer_config.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/tracing:null_span_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":tracer_config_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abi_impl",
        ":tracer_config_lib",
        "//envoy/server:tracer_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/tracers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

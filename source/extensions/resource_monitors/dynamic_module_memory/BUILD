load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dynamic_module_memory_monitor",
    srcs = ["dynamic_module_memory_monitor.cc"],
    hdrs = ["dynamic_module_memory_monitor.h"],
    deps = [
        "//envoy/server:resource_monitor_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/dynamic_modules:module_memory_lib",
        "@envoy_api//envoy/extensions/resource_monitors/dynamic_module_memory/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":dynamic_module_memory_monitor",
        "//envoy/registry",
        "//envoy/server:resource_monitor_config_interface",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/extensions/resource_monitors/dynamic_module_memory/v3:pkg_cc_proto",
    ],
)

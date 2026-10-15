load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "fixed_heap_monitor",
    srcs = ["fixed_heap_monitor.cc"],
    hdrs = ["fixed_heap_monitor.h"],
    deps = [
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/memory:stats_lib",
        "//source/common/runtime:runtime_protos_lib",
        "@abseil-cpp//absl/types:variant",
        "@envoy_api//envoy/extensions/resource_monitors/fixed_heap/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":fixed_heap_monitor",
        "//envoy/common:exception_lib",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/runtime:runtime_protos_lib",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/extensions/resource_monitors/fixed_heap/v3:pkg_cc_proto",
    ],
)

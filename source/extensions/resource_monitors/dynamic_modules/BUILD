load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "resource_monitor_lib",
    srcs = ["resource_monitor.cc"],
    hdrs = ["resource_monitor.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/server:resource_monitor_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":resource_monitor_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/strings",
    ],
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abi_impl",
        ":resource_monitor_lib",
        "//envoy/registry",
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/resource_monitors/dynamic_modules/v3:pkg_cc_proto",
    ],
)

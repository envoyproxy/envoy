load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("//bazel:apple.bzl", "envoy_objc_library")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_objc_library(
    name = "apple_network_change_monitor_lib",
    srcs = [
        "apple_network_change_monitor.mm",
        "apple_network_change_monitor_impl.mm",
    ],
    hdrs = [
        "apple_network_change_monitor.h",
        "apple_network_change_monitor_bridge.h",
    ],
    module_name = "AppleNetworkChangeMonitor",
    sdk_frameworks = [
        "CoreTelephony",
        "Network",
        "SystemConfiguration",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/cc:network_change_monitor_interface",
        "//library/common:engine_types_lib",
        "//library/common/network:network_types_lib",
        "//library/common/types:c_types_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "injected_resource_monitor",
    srcs = ["injected_resource_monitor.cc"],
    hdrs = ["injected_resource_monitor.h"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/extensions/resource_monitors/injected_resource/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":injected_resource_monitor",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/extensions/resource_monitors/injected_resource/v3:pkg_cc_proto",
    ],
)

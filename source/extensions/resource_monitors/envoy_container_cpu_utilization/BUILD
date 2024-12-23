load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "envoy_container_cpu_utilization_monitor",
    srcs = ["envoy_container_cpu_utilization_monitor.cc"],
    hdrs = [
        "container_stats_reader.h",
        "envoy_container_cpu_utilization_monitor.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/runtime:runtime_features_lib",
        "@envoy_api//envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "linux_container_stats_reader",
    srcs = ["linux_container_stats_reader.cc"],
    hdrs = [
        "container_stats_reader.h",
        "linux_container_stats_reader.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:logger_lib",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3:pkg_cc_proto",
    ],
)


envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    tags = ["skip_on_windows"],
    deps = [
        ":envoy_container_cpu_utilization_monitor",
        ":linux_container_stats_reader",
        "//envoy/registry",
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/common:logger_lib",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3:pkg_cc_proto",
    ],
)

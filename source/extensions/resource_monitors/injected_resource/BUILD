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
        "//include/envoy/api:api_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/config/resource_monitor/injected_resource/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # TODO(#9953) clean up.
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    security_posture = "data_plane_agnostic",
    status = "alpha",
    deps = [
        ":injected_resource_monitor",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/resource_monitors:well_known_names",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/config/resource_monitor/injected_resource/v2alpha:pkg_cc_proto",
    ],
)

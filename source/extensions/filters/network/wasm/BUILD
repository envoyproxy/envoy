load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Public docs: docs/root/configuration/network_filters/wasm_filter.rst

envoy_cc_library(
    name = "wasm_filter_lib",
    srcs = ["wasm_filter.cc"],
    hdrs = ["wasm_filter.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/extensions/filters/network/wasm/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        ":wasm_filter_lib",
        "//include/envoy/registry",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/wasm/v3:pkg_cc_proto",
    ],
)

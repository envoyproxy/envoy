load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/wasm_filter

envoy_cc_library(
    name = "wasm_filter_lib",
    srcs = ["wasm_filter.cc"],
    hdrs = ["wasm_filter.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/extensions/common/wasm:remote_async_datasource_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "@envoy_api//envoy/extensions/filters/http/wasm/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":wasm_filter_lib",
        "//envoy/registry",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/wasm/v3:pkg_cc_proto",
    ],
)

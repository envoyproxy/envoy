load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Stats sink for wasm

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":wasm_stat_sink_lib",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/server:instance_interface",
        "//source/extensions/common/wasm:remote_async_datasource_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/extensions/stat_sinks/wasm/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "wasm_stat_sink_lib",
    hdrs = ["wasm_stat_sink_impl.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/extensions/common/wasm:wasm_lib",
        "@envoy_api//envoy/extensions/filters/network/wasm/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

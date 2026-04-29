load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "wasm_filter_stat_sink_lib",
    srcs = ["wasm_filter_stat_sink_impl.cc"],
    hdrs = [
        "enriched_metric.h",
        "wasm_filter_stat_sink_impl.h",
    ],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/common:logger_lib",
        "//source/extensions/common/wasm:wasm_lib",
    ],
    alwayslink = 1,
)

envoy_cc_contrib_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":wasm_filter_stat_sink_lib",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/common/wasm:remote_async_datasource_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/server:configuration_lib",
        "@envoy_api//contrib/envoy/extensions/stat_sinks/wasm_filter/v3:pkg_cc_proto",
    ],
)

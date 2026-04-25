load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary")

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test_library(
    name = "stats_filter_test_plugin",
    srcs = [
        "stats_filter_plugin.cc",
        "stats_filter_plugin_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm/ext:envoy_null_plugin",
    ],
)

envoy_wasm_cc_binary(
    name = "stats_filter_plugin.wasm",
    srcs = ["stats_filter_plugin.cc"],
    deps = [
        "//source/extensions/common/wasm/ext:envoy_proxy_wasm_api_lib",
    ],
)

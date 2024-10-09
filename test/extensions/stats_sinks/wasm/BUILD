load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//bazel:envoy_select.bzl",
    "envoy_select_wasm_cpp_tests",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/stats_sinks/wasm/test_data:test_context_cpp.wasm",
    ]),
    extension_names = ["envoy.stat_sinks.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/stat_sinks/wasm:config",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/extensions/stats_sinks/wasm/test_data:test_context_cpp_plugin",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/stat_sinks/wasm/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "wasm_stat_sink_test",
    srcs = ["wasm_stat_sink_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/stats_sinks/wasm/test_data:test_context_cpp.wasm",
    ]),
    extension_names = ["envoy.stat_sinks.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/stats:stats_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/wasm_runtime/null:config",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/extensions/stats_sinks/wasm/test_data:test_context_cpp_plugin",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:wasm_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

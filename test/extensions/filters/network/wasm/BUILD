load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//bazel:envoy_select.bzl",
    "envoy_select_wasm_cpp_tests",
    "envoy_select_wasm_rust_tests",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    size = "enormous",
    srcs = ["config_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/filters/network/wasm/test_data:test_cpp.wasm",
    ]),
    extension_names = ["envoy.filters.network.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/filters/network/wasm:config",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/extensions/filters/network/wasm/test_data:test_cpp_plugin",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/filters/network/wasm/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "wasm_filter_test",
    size = "enormous",
    srcs = ["wasm_filter_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/filters/network/wasm/test_data:test_cpp.wasm",
    ]) + envoy_select_wasm_rust_tests([
        "//test/extensions/filters/network/wasm/test_data:close_stream_rust.wasm",
        "//test/extensions/filters/network/wasm/test_data:logging_rust.wasm",
        "//test/extensions/filters/network/wasm/test_data:panic_rust.wasm",
        "//test/extensions/filters/network/wasm/test_data:resume_call_rust.wasm",
    ]),
    extension_names = ["envoy.filters.network.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/network/wasm:wasm_filter_lib",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/extensions/filters/network/wasm/test_data:test_cpp_plugin",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:wasm_lib",
    ],
)

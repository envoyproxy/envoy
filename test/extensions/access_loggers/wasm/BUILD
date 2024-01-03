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
        "//test/extensions/access_loggers/wasm/test_data:test_cpp.wasm",
    ]),
    extension_names = ["envoy.access_loggers.wasm"],
    deps = [
        "//source/extensions/access_loggers/wasm:config",
        "//test/extensions/access_loggers/wasm/test_data:test_cpp_plugin",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/access_loggers/wasm/v3:pkg_cc_proto",
    ],
)

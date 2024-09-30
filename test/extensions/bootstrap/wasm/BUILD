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
    "envoy_extension_cc_test_binary",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "wasm_test",
    srcs = ["wasm_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/bootstrap/wasm/test_data:asm2wasm_cpp.wasm",
        "//test/extensions/bootstrap/wasm/test_data:bad_signature_cpp.wasm",
        "//test/extensions/bootstrap/wasm/test_data:emscripten_cpp.wasm",
        "//test/extensions/bootstrap/wasm/test_data:logging_cpp.wasm",
        "//test/extensions/bootstrap/wasm/test_data:segv_cpp.wasm",
        "//test/extensions/bootstrap/wasm/test_data:stats_cpp.wasm",
    ]) + envoy_select_wasm_rust_tests([
        "//test/extensions/bootstrap/wasm/test_data:logging_rust.wasm",
    ]),
    extension_names = ["envoy.bootstrap.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/bootstrap/wasm:config",
        "//source/extensions/common/wasm:wasm_lib",
        "//test/extensions/bootstrap/wasm/test_data:stats_cpp_plugin",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_extension_cc_test(
    name = "wasm_integration_test",
    size = "large",
    srcs = ["wasm_integration_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/bootstrap/wasm/test_data:http_cpp.wasm",
    ]),
    extension_names = ["envoy.bootstrap.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/bootstrap/wasm:config",
        "//source/extensions/common/wasm:wasm_lib",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/integration:http_protocol_integration_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/bootstrap/wasm/test_data:missing_cpp.wasm",
        "//test/extensions/bootstrap/wasm/test_data:start_cpp.wasm",
    ]),
    extension_names = ["envoy.bootstrap.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/registry",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/bootstrap/wasm:config",
        "//source/extensions/common/wasm:wasm_lib",
        "//test/extensions/bootstrap/wasm/test_data:start_cpp_plugin",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test_binary(
    name = "wasm_speed_test",
    srcs = ["wasm_speed_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/bootstrap/wasm/test_data:speed_cpp.wasm",
    ]),
    extension_names = ["envoy.bootstrap.wasm"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/bootstrap/wasm:config",
        "//source/extensions/common/wasm:wasm_lib",
        "//test/extensions/bootstrap/wasm/test_data:speed_cpp_plugin",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "@com_github_google_benchmark//:benchmark",
        "@com_google_absl//absl/types:optional",
    ],
)

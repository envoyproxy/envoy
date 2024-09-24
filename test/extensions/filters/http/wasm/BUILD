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
    name = "wasm_filter_test",
    srcs = ["wasm_filter_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/filters/http/wasm/test_data:test_cpp.wasm",
    ]) + envoy_select_wasm_rust_tests([
        "//test/extensions/filters/http/wasm/test_data:async_call_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:body_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:close_stream_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:grpc_call_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:grpc_stream_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:headers_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:metadata_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:panic_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:resume_call_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:shared_data_rust.wasm",
        "//test/extensions/filters/http/wasm/test_data:shared_queue_rust.wasm",
    ]),
    extension_names = ["envoy.filters.http.wasm"],
    rbe_pool = "2core",
    shard_count = 50,
    tags = [
        "cpu:4",
        "skip_on_windows",
    ],
    deps = [
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/wasm:wasm_filter_lib",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/extensions/filters/http/wasm/test_data:test_cpp_plugin",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/router:router_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:wasm_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    size = "large",
    srcs = ["config_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/filters/http/wasm/test_data:test_cpp.wasm",
    ]),
    extension_names = ["envoy.filters.http.wasm"],
    rbe_pool = "2core",
    shard_count = 16,
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/filters/http/wasm:config",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/filters/http/wasm/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "wasm_filter_integration_test",
    srcs = ["wasm_filter_integration_test.cc"],
    data = envoy_select_wasm_cpp_tests([
        "//test/extensions/filters/http/wasm/test_data:test_cpp.wasm",
    ]),
    extension_names = ["envoy.filters.http.wasm"],
    rbe_pool = "2core",
    shard_count = 16,
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/filters/http/wasm:config",
        "//test/extensions/common/wasm:wasm_runtime",
        "//test/integration:http_integration_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

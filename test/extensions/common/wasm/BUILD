licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "wasm_vm_test",
    srcs = ["wasm_vm_test.cc"],
    data = [
        "//test/extensions/common/wasm/test_data:modules",
    ],
    deps = [
        "//source/extensions/common/wasm:wasm_vm_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "wasm_test",
    srcs = ["wasm_test.cc"],
    data = [
        "//test/extensions/common/wasm/test_data:modules",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//test/extensions/common/wasm/test_data:test_cpp_plugin",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "wasm_vm_test",
    srcs = ["wasm_vm_test.cc"],
    tags = [
        "manual",
    ],
    deps = [
        "//source/extensions/common/wasm:wasm_vm_lib",
        "//test/test_common:utility_lib",
    ],
)

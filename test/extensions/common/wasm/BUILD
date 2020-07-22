load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "wasm_vm_test",
    srcs = ["wasm_vm_test.cc"],
    data = [
        "//test/extensions/common/wasm/test_data:modules",
    ],
    # wasm (wee v8 etc) will not compile on Windows
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/wasm:wasm_vm_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

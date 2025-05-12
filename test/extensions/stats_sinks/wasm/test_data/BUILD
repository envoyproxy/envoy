load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "test_context_cpp_plugin",
    srcs = [
        "test_context_cpp.cc",
        "test_context_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm/ext:envoy_null_plugin",
    ],
)

envoy_wasm_cc_binary(
    name = "test_context_cpp.wasm",
    srcs = ["test_context_cpp.cc"],
    deps = [
        "//source/extensions/common/wasm/ext:envoy_proxy_wasm_api_lib",
    ],
)

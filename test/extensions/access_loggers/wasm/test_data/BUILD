load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "test_cpp_plugin",
    srcs = [
        "test_cpp.cc",
        "test_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "@com_google_absl//absl/container:node_hash_map",
    ],
)

envoy_wasm_cc_binary(
    name = "test_cpp.wasm",
    srcs = ["test_cpp.cc"],
)

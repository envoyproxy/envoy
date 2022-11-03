load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel:envoy_select.bzl", "envoy_select_wasm_v8_bool")
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary", "wasm_rust_binary")

licenses(["notice"])  # Apache 2

envoy_package()

wasm_rust_binary(
    name = "test_rust.wasm",
    srcs = ["test_rust.rs"],
    rustc_flags = ["-Clink-arg=-zstack-size=32768"],
    precompile = envoy_select_wasm_v8_bool(),
)

envoy_cc_test_library(
    name = "test_cpp_plugin",
    srcs = [
        "test_cpp.cc",
        "test_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//external:abseil_node_hash_map",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
    ],
)

envoy_cc_test_library(
    name = "test_context_cpp_plugin",
    srcs = [
        "test_context_cpp.cc",
        "test_context_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//external:abseil_node_hash_map",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm/ext:envoy_null_plugin",
    ],
)

envoy_cc_test_library(
    name = "test_restriction_cpp_plugin",
    srcs = [
        "test_restriction_cpp.cc",
        "test_restriction_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//external:abseil_node_hash_map",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm/ext:envoy_null_plugin",
    ],
)

envoy_wasm_cc_binary(
    name = "test_cpp.wasm",
    srcs = ["test_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "test_context_cpp.wasm",
    srcs = ["test_context_cpp.cc"],
    deps = [
        "//source/extensions/common/wasm/ext:envoy_proxy_wasm_api_lib",
    ],
)

envoy_wasm_cc_binary(
    name = "test_restriction_cpp.wasm",
    srcs = ["test_restriction_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "bad_signature_cpp.wasm",
    srcs = ["bad_signature_cpp.cc"],
)

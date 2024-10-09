load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary", "wasm_rust_binary")

licenses(["notice"])  # Apache 2

envoy_package()

wasm_rust_binary(
    name = "close_stream_rust.wasm",
    srcs = ["close_stream_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo/remote:log",
    ],
)

wasm_rust_binary(
    name = "logging_rust.wasm",
    srcs = ["logging_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo/remote:log",
    ],
)

wasm_rust_binary(
    name = "panic_rust.wasm",
    srcs = ["panic_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo/remote:log",
    ],
)

wasm_rust_binary(
    name = "resume_call_rust.wasm",
    srcs = ["resume_call_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo/remote:log",
    ],
)

envoy_cc_test_library(
    name = "test_cpp_plugin",
    srcs = [
        "test_close_stream_cpp.cc",
        "test_cpp.cc",
        "test_cpp_null_plugin.cc",
        "test_resume_call_cpp.cc",
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
    srcs = [
        "test_close_stream_cpp.cc",
        "test_cpp.cc",
        "test_panic_cpp.cc",
        "test_resume_call_cpp.cc",
    ],
)

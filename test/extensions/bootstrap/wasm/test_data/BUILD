load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary", "wasm_rust_binary")

licenses(["notice"])  # Apache 2

envoy_package()

wasm_rust_binary(
    name = "logging_rust.wasm",
    srcs = ["logging_rust.rs"],
    wasi = True,
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo/remote:log",
    ],
)

envoy_cc_test_library(
    name = "speed_cpp_plugin",
    srcs = [
        "speed_cpp.cc",
        "speed_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "@com_google_absl//absl/container:node_hash_map",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "start_cpp_plugin",
    srcs = [
        "start_cpp.cc",
        "start_cpp_null_plugin.cc",
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

envoy_cc_test_library(
    name = "stats_cpp_plugin",
    srcs = [
        "stats_cpp.cc",
        "stats_cpp_null_plugin.cc",
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
    name = "asm2wasm_cpp.wasm",
    srcs = ["asm2wasm_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "bad_signature_cpp.wasm",
    srcs = ["bad_signature_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "emscripten_cpp.wasm",
    srcs = ["emscripten_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "http_cpp.wasm",
    srcs = ["http_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "logging_cpp.wasm",
    srcs = ["logging_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "missing_cpp.wasm",
    srcs = ["missing_cpp.cc"],
    linkopts = [
        "-s ERROR_ON_UNDEFINED_SYMBOLS=0",
    ],
)

envoy_wasm_cc_binary(
    name = "segv_cpp.wasm",
    srcs = ["segv_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "speed_cpp.wasm",
    srcs = ["speed_cpp.cc"],
    protobuf = "full",
)

envoy_wasm_cc_binary(
    name = "start_cpp.wasm",
    srcs = ["start_cpp.cc"],
)

envoy_wasm_cc_binary(
    name = "stats_cpp.wasm",
    srcs = ["stats_cpp.cc"],
)

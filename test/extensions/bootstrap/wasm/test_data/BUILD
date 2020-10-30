load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary", "wasm_rust_binary")

licenses(["notice"])  # Apache 2

envoy_package()

wasm_rust_binary(
    name = "logging_rust.wasm",
    srcs = ["logging_rust.rs"],
    deps = [
        "//bazel/external/cargo:log",
        "//bazel/external/cargo:proxy_wasm",
    ],
)

envoy_cc_library(
    name = "speed_cpp_plugin",
    srcs = [
        "speed_cpp.cc",
        "speed_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//external:abseil_node_hash_map",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "start_cpp_plugin",
    srcs = [
        "start_cpp.cc",
        "start_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//external:abseil_node_hash_map",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm:well_known_names",
    ],
)

envoy_cc_library(
    name = "stats_cpp_plugin",
    srcs = [
        "stats_cpp.cc",
        "stats_cpp_null_plugin.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        "//external:abseil_node_hash_map",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm:well_known_names",
    ],
)

envoy_wasm_cc_binary(
    name = "asm2wasm_cpp.wasm",
    srcs = ["asm2wasm_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

envoy_wasm_cc_binary(
    name = "bad_signature_cpp.wasm",
    srcs = ["bad_signature_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

envoy_wasm_cc_binary(
    name = "emscripten_cpp.wasm",
    srcs = ["emscripten_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

envoy_wasm_cc_binary(
    name = "logging_cpp.wasm",
    srcs = ["logging_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

envoy_wasm_cc_binary(
    name = "missing_cpp.wasm",
    srcs = ["missing_cpp.cc"],
    linkopts = [
        "--js-library external/proxy_wasm_cpp_sdk/proxy_wasm_intrinsics.js",
        "-s ERROR_ON_UNDEFINED_SYMBOLS=0",
    ],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

envoy_wasm_cc_binary(
    name = "segv_cpp.wasm",
    srcs = ["segv_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

envoy_wasm_cc_binary(
    name = "speed_cpp.wasm",
    srcs = ["speed_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics_full",
    ],
)

envoy_wasm_cc_binary(
    name = "start_cpp.wasm",
    srcs = ["start_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

envoy_wasm_cc_binary(
    name = "stats_cpp.wasm",
    srcs = ["stats_cpp.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

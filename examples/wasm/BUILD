load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load("//bazel/wasm:wasm.bzl", "wasm_cc_binary")

licenses(["notice"])  # Apache 2

envoy_package()

wasm_cc_binary(
    name = "envoy_filter_http_wasm_example.wasm",
    srcs = ["envoy_filter_http_wasm_example.cc"],
    deps = [
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
)

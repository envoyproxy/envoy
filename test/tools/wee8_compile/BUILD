load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_package",
)
load("//bazel:envoy_select.bzl", "envoy_select_wasm_v8")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_binary(
    name = "wee8_compile_tool",
    srcs = select({
        "@proxy-wasm-cpp-host//bazel:engine_wamr_interp": ["wee8_compile_stub.cc"],
        "@proxy-wasm-cpp-host//bazel:engine_wamr_jit": ["wee8_compile_stub.cc"],
        "@proxy-wasm-cpp-host//bazel:engine_wasmtime": ["wee8_compile_stub.cc"],
        "@proxy-wasm-cpp-host//bazel:engine_disabled": ["wee8_compile_stub.cc"],
        "//conditions:default": [],
    }),
    stamp = 0,
    deps = envoy_select_wasm_v8([":wee8_compile_lib"]),
)

envoy_cc_library(
    name = "wee8_compile_lib",
    srcs = envoy_select_wasm_v8(["wee8_compile.cc"]),
    copts = [
        "-Wno-comments",
        "-Wno-non-virtual-dtor",
        "-Wno-unused-parameter",
    ],
    deps = envoy_select_wasm_v8([
        # abseil is excluded from the prebuilt wee8; supply it at the consumer.
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/synchronization",
        "@proxy-wasm-cpp-host//:base_lib",
        "@proxy-wasm-cpp-host//:v8_lib",
    ]),
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)
load("//bazel:envoy_select.bzl", "envoy_select_wasm_wasmtime")

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = envoy_select_wasm_wasmtime(["config.cc"]),
    deps = envoy_select_wasm_wasmtime([
        "//envoy/registry",
        "//source/extensions/common/wasm:wasm_runtime_factory_interface",
        "@proxy_wasm_cpp_host//:base_lib",
        "@proxy_wasm_cpp_host//:wasmtime_lib",
    ]),
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)
load("//bazel:envoy_select.bzl", "envoy_select_wasm_wamr")

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = envoy_select_wasm_wamr(["config.cc"]),
    deps = envoy_select_wasm_wamr([
        "//envoy/registry",
        "//source/extensions/common/wasm:wasm_runtime_factory_interface",
        "@proxy-wasm-cpp-host//:base_lib",
        "@proxy-wasm-cpp-host//:wamr_lib",
    ]),
)

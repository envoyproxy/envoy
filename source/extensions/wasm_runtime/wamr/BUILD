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
    srcs = ["config.cc"],
    category = "envoy.wasm.runtime",
    security_posture = "unknown",
    status = "alpha",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/common/wasm:wasm_runtime_factory_interface",
    ] + envoy_select_wasm_wamr([
        "@proxy_wasm_cpp_host//:wamr_lib",
    ]),
)

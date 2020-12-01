load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)
load("//bazel:envoy_select.bzl", "envoy_select_wasm_v8")

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/common/wasm:wasm_runtime_factory_interface",
    ] + envoy_select_wasm_v8([
        "@proxy_wasm_cpp_host//:v8_lib",
    ]),
)

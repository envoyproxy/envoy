load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        "//envoy/registry",
        "//source/extensions/common/wasm:wasm_runtime_factory_interface",
        "@proxy-wasm-cpp-host//:base_lib",
        "@proxy-wasm-cpp-host//:null_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "v8_lib",
    srcs = ["v8.cc"],
    hdrs = ["v8.h"],
    external_deps = [
        "wee8",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/extensions/common/wasm:wasm_vm_base",
        "//source/extensions/common/wasm:wasm_vm_interface",
        "//source/extensions/common/wasm:well_known_names",
    ],
)

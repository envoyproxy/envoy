licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "wasm_vm_interface",
    hdrs = ["wasm_vm.h"],
    deps = [
        ":well_known_names",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "wasm_vm_lib",
    srcs = ["wasm_vm.cc"],
    deps = [
        ":wasm_vm_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/common/wasm/null:null_lib",
    ],
)

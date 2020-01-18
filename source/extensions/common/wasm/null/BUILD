licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "null_vm_plugin_interface",
    hdrs = ["null_vm_plugin.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//source/extensions/common/wasm:wasm_vm_interface",
        "//source/extensions/common/wasm:well_known_names",
    ],
)

envoy_cc_library(
    name = "null_vm_lib",
    srcs = ["null_vm.cc"],
    hdrs = ["null_vm.h"],
    deps = [
        ":null_vm_plugin_interface",
        "//external:abseil_node_hash_map",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/common/wasm:wasm_vm_base",
        "//source/extensions/common/wasm:wasm_vm_interface",
        "//source/extensions/common/wasm:well_known_names",
    ],
)

envoy_cc_library(
    name = "null_lib",
    srcs = ["null.cc"],
    hdrs = ["null.h"],
    deps = [
        ":null_vm_lib",
        ":null_vm_plugin_interface",
        "//external:abseil_node_hash_map",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/common/wasm:wasm_vm_interface",
        "//source/extensions/common/wasm:well_known_names",
    ],
)

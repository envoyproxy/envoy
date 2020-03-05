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
    name = "wasm_vm_lib",
    srcs = ["wasm_vm.cc"],
    deps = [
        ":wasm_hdr",
        "@proxy_wasm_cpp_host//:lib",
        "@proxy_wasm_cpp_sdk//:common_lib",
        "//source/common/common:assert_lib",
        "//source/common/stats:stats_lib",
        #"//source/extensions/common/wasm/v8:v8_lib",
    ],
)

# NB: Used to break the circular dependency between wasm_lib and null_plugin_lib.
envoy_cc_library(
    name = "wasm_hdr",
    hdrs = [
        "context.h",
        "wasm.h",
        "wasm_vm.h",
    ],
    deps = [
        "@proxy_wasm_cpp_host//:include",
        "@proxy_wasm_cpp_sdk//:common_lib",
        ":well_known_names",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/config:datasource_lib",
        "//source/common/stats:stats_lib",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "wasm_interoperation_lib",
    srcs = ["wasm_state.cc"],
    hdrs = ["wasm_state.h"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "wasm_lib",
    srcs = [
        "context.cc",
        "wasm.cc",
    ],
    deps = [
        ":wasm_hdr",
        ":wasm_interoperation_lib",
        ":wasm_vm_lib",
        "//external:abseil_base",
        "//external:abseil_node_hash_map",
        "//source/common/common:base64_lib",
        "//source/common/common:enum_to_int",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
    ],
)

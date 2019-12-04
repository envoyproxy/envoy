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
    name = "wasm_vm_base",
    hdrs = ["wasm_vm_base.h"],
    deps = [
        "//source/common/stats:stats_lib",
    ],
)

envoy_cc_library(
    name = "wasm_vm_lib",
    srcs = ["wasm_vm.cc"],
    deps = [
        ":wasm_vm_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/common/wasm/null:null_lib",
        "//source/extensions/common/wasm/v8:v8_lib",
    ],
)

envoy_cc_library(
    name = "api_common_hdrs",
    hdrs = [
        "@envoy_api//wasm/cpp:common_hdrs",
    ],
    strip_include_prefix="//external/envoy_api/wasm/cpp",
)

# NB: Used to break the circular dependency between wasm_lib and null_plugin_lib.
envoy_cc_library(
    name = "wasm_hdr",
    hdrs = [
        "context.h",
        "exports.h",
        "wasm.h",
    ],
    deps = [
        ":api_common_hdrs",
        ":wasm_vm_interface",
        ":well_known_names",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:stack_array",
        "//source/common/config:datasource_lib",
        "//source/common/stats:stats_lib",
    ],
)


envoy_cc_library(
    name = "wasm_interoperation_lib",
    srcs = [
        "wasm_state.cc",
    ],
    hdrs = [
        "wasm_state.h",
    ],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "wasm_lib",
    srcs = [
        "context.cc",
        "exports.cc",
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
        "//source/extensions/common/wasm/null:null_lib",
        "@envoy_api//envoy/config/wasm/v2:pkg_cc_proto",
    ],
)

load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "envoy_null_vm_wasm_api",
    hdrs = [
        "envoy_null_vm_wasm_api.h",
        "envoy_proxy_wasm_api.h",
    ],
    deps = [
        "@proxy_wasm_cpp_sdk//:api_lib",
        "@proxy_wasm_cpp_sdk//:common_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "envoy_null_plugin",
    hdrs = [
        "envoy_null_plugin.h",
        "envoy_proxy_wasm_api.h",
    ],
    deps = [
        ":declare_property_cc_proto",
        ":set_envoy_filter_state_cc_proto",
        ":verify_signature_cc_proto",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

# NB: this target is compiled to Wasm. Hence the generic rule.
cc_library(
    name = "envoy_proxy_wasm_api_lib",
    srcs = ["envoy_proxy_wasm_api.cc"],
    hdrs = ["envoy_proxy_wasm_api.h"],
    tags = ["manual"],
    deps = [
        ":declare_property_cc_proto",
        ":node_subset_cc_proto",
        ":set_envoy_filter_state_cc_proto",
        ":verify_signature_cc_proto",
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
    alwayslink = 1,
)

filegroup(
    name = "envoy_proxy_wasm_api_js",
    srcs = ["envoy_proxy_wasm_api.js"],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
proto_library(
    name = "declare_property_proto",
    srcs = ["declare_property.proto"],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
cc_proto_library(
    name = "declare_property_cc_proto",
    deps = [":declare_property_proto"],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
proto_library(
    name = "verify_signature_proto",
    srcs = ["verify_signature.proto"],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
cc_proto_library(
    name = "verify_signature_cc_proto",
    deps = [":verify_signature_proto"],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
proto_library(
    name = "node_subset_proto",
    srcs = ["node_subset.proto"],
    deps = [
        "@com_google_protobuf//:struct_proto",
    ],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
cc_proto_library(
    name = "node_subset_cc_proto",
    deps = [
        ":node_subset_proto",
        # "@com_google_protobuf//:protoc_lib",
    ],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
proto_library(
    name = "set_envoy_filter_state_proto",
    srcs = ["set_envoy_filter_state.proto"],
    deps = [
        "declare_property_proto",
    ],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
cc_proto_library(
    name = "set_envoy_filter_state_cc_proto",
    deps = [":set_envoy_filter_state_proto"],
)

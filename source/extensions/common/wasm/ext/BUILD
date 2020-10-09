load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library")
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
    visibility = ["//visibility:public"],
    deps = [
        "@proxy_wasm_cpp_sdk//:api_lib",
        "@proxy_wasm_cpp_sdk//:common_lib",
    ],
)

envoy_cc_library(
    name = "envoy_null_plugin",
    hdrs = [
        "envoy_null_plugin.h",
        "envoy_proxy_wasm_api.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":declare_property_cc_proto",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

# NB: this target is compiled to Wasm. Hence the generic rule.
cc_library(
    name = "envoy_proxy_wasm_api_lib",
    srcs = ["envoy_proxy_wasm_api.cc"],
    hdrs = ["envoy_proxy_wasm_api.h"],
    tags = ["manual"],
    visibility = ["//visibility:public"],
    deps = [
        ":declare_property_cc_proto",
        ":node_subset_cc_proto",
        "@proxy_wasm_cpp_sdk//:proxy_wasm_intrinsics",
    ],
    alwayslink = 1,
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
proto_library(
    name = "declare_property_proto",
    srcs = ["declare_property.proto"],
    visibility = ["//visibility:public"],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
cc_proto_library(
    name = "declare_property_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":declare_property_proto"],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
proto_library(
    name = "node_subset_proto",
    srcs = ["node_subset.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:struct_proto",
    ],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
cc_proto_library(
    name = "node_subset_cc_proto",
    visibility = ["//visibility:public"],
    deps = [
        ":node_subset_proto",
        # "//external:protobuf_clib",
    ],
)

filegroup(
    name = "jslib",
    srcs = [
        "envoy_wasm_intrinsics.js",
    ],
    visibility = ["//visibility:public"],
)

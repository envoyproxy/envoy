load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel/wasm:wasm.bzl", "envoy_wasm_cc_binary", "wasm_rust_binary")

licenses(["notice"])  # Apache 2

envoy_package()

wasm_rust_binary(
    name = "async_call_rust.wasm",
    srcs = ["async_call_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "body_rust.wasm",
    srcs = ["body_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "close_stream_rust.wasm",
    srcs = ["close_stream_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "grpc_call_rust.wasm",
    srcs = ["grpc_call_rust.rs"],
    deps = [
        "//bazel/external/cargo:protobuf",
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "grpc_stream_rust.wasm",
    srcs = ["grpc_stream_rust.rs"],
    deps = [
        "//bazel/external/cargo:protobuf",
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "headers_rust.wasm",
    srcs = ["headers_rust.rs"],
    wasi = True,
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "metadata_rust.wasm",
    srcs = ["metadata_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "panic_rust.wasm",
    srcs = ["panic_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "resume_call_rust.wasm",
    srcs = ["resume_call_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "shared_data_rust.wasm",
    srcs = ["shared_data_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

wasm_rust_binary(
    name = "shared_queue_rust.wasm",
    srcs = ["shared_queue_rust.rs"],
    deps = [
        "@proxy_wasm_rust_sdk//:proxy_wasm",
        "@proxy_wasm_rust_sdk//bazel/cargo:log",
    ],
)

envoy_cc_test_library(
    name = "test_cpp_plugin",
    srcs = [
        "test_async_call_cpp.cc",
        "test_body_cpp.cc",
        "test_close_stream_cpp.cc",
        "test_cpp.cc",
        "test_cpp_null_plugin.cc",
        "test_grpc_call_cpp.cc",
        "test_grpc_stream_cpp.cc",
        "test_resume_call_cpp.cc",
        "test_shared_data_cpp.cc",
        "test_shared_queue_cpp.cc",
    ],
    copts = ["-DNULL_PLUGIN=1"],
    deps = [
        ":test_cc_proto",
        "//external:abseil_node_hash_map",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/extensions/common/wasm:wasm_hdr",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/common/wasm/ext:envoy_null_plugin",
        "@proxy_wasm_cpp_sdk//contrib:contrib_lib",
    ],
)

envoy_wasm_cc_binary(
    name = "test_cpp.wasm",
    srcs = [
        "test_async_call_cpp.cc",
        "test_body_cpp.cc",
        "test_close_stream_cpp.cc",
        "test_cpp.cc",
        "test_grpc_call_cpp.cc",
        "test_grpc_stream_cpp.cc",
        "test_panic_cpp.cc",
        "test_resume_call_cpp.cc",
        "test_shared_data_cpp.cc",
        "test_shared_queue_cpp.cc",
    ],
    protobuf = "lite",
    deps = [
        ":test_cc_proto",
        "//source/extensions/common/wasm/ext:declare_property_cc_proto",
        "//source/extensions/common/wasm/ext:envoy_proxy_wasm_api_lib",
        "@proxy_wasm_cpp_sdk//contrib:contrib_lib",
    ],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

# NB: this target is compiled both to native code and to Wasm. Hence the generic rule.
cc_proto_library(
    name = "test_cc_proto",
    deps = [":test_proto"],
)

# TODO: FIXME
#
#filegroup(
#    name = "wavm_binary",
#    srcs = ["//bazel/foreign_cc:wavm"],
#    output_group = "wavm",
#)
#
#genrule(
#    name = "test_cpp_wavm_compile",
#    srcs = [":test_cpp.wasm"],
#    outs = ["test_cpp.wavm_compiled.wasm"],
#    cmd = "./$(location wavm_binary) compile $(location test_cpp.wasm) $(location test_cpp.wavm_compiled.wasm)",
#    tools = [
#        ":test_cpp.wasm",
#        ":wavm_binary",
#    ],
#)

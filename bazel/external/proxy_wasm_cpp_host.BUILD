load("@rules_cc//cc:defs.bzl", "cc_library")
load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_select_wasm",
    "envoy_select_wasm_v8",
    "envoy_select_wasm_wasmtime",
    "envoy_select_wasm_wavm",
)

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "include",
    hdrs = glob(["include/proxy-wasm/**/*.h"]),
    deps = [
        "@proxy_wasm_cpp_sdk//:common_lib",
    ],
)

cc_library(
    name = "lib",
    # Note that the select cannot appear in the glob.
    srcs = envoy_select_wasm([
        "include/proxy-wasm/context.h",
        "include/proxy-wasm/context_interface.h",
        "include/proxy-wasm/exports.h",
        "include/proxy-wasm/null.h",
        "include/proxy-wasm/null_plugin.h",
        "include/proxy-wasm/null_vm.h",
        "include/proxy-wasm/null_vm_plugin.h",
        "include/proxy-wasm/wasm.h",
        "include/proxy-wasm/wasm_api_impl.h",
        "include/proxy-wasm/wasm_vm.h",
        "include/proxy-wasm/word.h",
        "src/context.cc",
        "src/exports.cc",
        "src/foreign.cc",
        "src/wasm.cc",
        "src/null/null.cc",
        "src/null/null_plugin.cc",
        "src/null/null_vm.cc",
        "src/third_party/base64.cc",
        "src/third_party/base64.h",
        "src/third_party/picosha2.h",
    ]) + envoy_select_wasm_v8([
        "include/proxy-wasm/v8.h",
        "src/v8/v8.cc",
    ]) + envoy_select_wasm_wavm([
        "include/proxy-wasm/wavm.h",
        "src/wavm/wavm.cc",
    ]) + envoy_select_wasm_wasmtime([
        "include/proxy-wasm/wasmtime.h",
        "src/common/types.h",
        "src/wasmtime/types.h",
        "src/wasmtime/wasmtime.cc",
    ]),
    copts = envoy_select_wasm_wavm([
        '-DWAVM_API=""',
        "-Wno-non-virtual-dtor",
        "-Wno-old-style-cast",
    ]),
    deps = [
        ":include",
        "//external:abseil_flat_hash_map",
        "//external:abseil_optional",
        "//external:abseil_strings",
        "//external:protobuf",
        "//external:ssl",
        "//external:zlib",
        "@proxy_wasm_cpp_sdk//:api_lib",
        "@proxy_wasm_cpp_sdk//:common_lib",
    ] + envoy_select_wasm_v8([
        "//external:wee8",
    ]) + envoy_select_wasm_wavm([
        "@envoy//bazel/foreign_cc:wavm",
    ]) + envoy_select_wasm_wasmtime([
        "@com_github_wasm_c_api//:wasmtime_lib",
    ]),
)

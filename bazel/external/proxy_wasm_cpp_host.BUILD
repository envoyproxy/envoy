load("@rules_cc//cc:defs.bzl", "cc_library")

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
    name = "common_lib",
    srcs = glob([
        "src/*.h",
        "src/*.cc",
        "src/common/*.h",
        "src/common/*.cc",
        "src/third_party/*.h",
        "src/third_party/*.cc",
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
    ],
)

cc_library(
    name = "null_lib",
    srcs = glob([
        "src/null/*.cc",
    ]),
    deps = [
        ":common_lib",
    ],
)

cc_library(
    name = "v8_lib",
    srcs = glob([
        "src/v8/*.cc",
    ]),
    deps = [
        ":common_lib",
        "//external:wee8",
    ],
)

cc_library(
    name = "wamr_lib",
    srcs = glob([
        "src/wamr/*.h",
        "src/wamr/*.cc",
    ]),
    deps = [
        ":common_lib",
        "@com_github_wamr//:wamr_lib",
    ],
)

cc_library(
    name = "wavm_lib",
    srcs = glob([
        "src/wavm/*.cc",
    ]),
    copts = [
        '-DWAVM_API=""',
        "-Wno-non-virtual-dtor",
        "-Wno-old-style-cast",
    ],
    deps = [
        ":common_lib",
        "@envoy//bazel/foreign_cc:wavm",
    ],
)

cc_library(
    name = "wasmtime_lib",
    srcs = glob([
        "src/wasmtime/*.h",
        "src/wasmtime/*.cc",
    ]),
    deps = [
        ":common_lib",
        "@com_github_wasm_c_api//:wasmtime_lib",
    ],
)

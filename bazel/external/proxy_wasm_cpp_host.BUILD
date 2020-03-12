licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "include",
    hdrs = [
        "include/proxy-wasm/compat.h",
        "include/proxy-wasm/context.h",
        "include/proxy-wasm/exports.h",
        "include/proxy-wasm/null.h",
        "include/proxy-wasm/null_plugin.h",
        "include/proxy-wasm/null_vm.h",
        "include/proxy-wasm/null_vm_plugin.h",
        "include/proxy-wasm/v8.h",
        "include/proxy-wasm/wasm.h",
        "include/proxy-wasm/wasm_api_impl.h",
        "include/proxy-wasm/wasm_vm.h",
        "include/proxy-wasm/word.h",
    ],
    copts = ["-std=c++14"],
    deps = [
        "@proxy_wasm_cpp_sdk//:common_lib",
    ],
)

cc_library(
    name = "lib",
    srcs = [
        "src/base64.cc",
        "src/base64.h",
        "src/context.cc",
        "src/exports.cc",
        "src/foreign.cc",
        "src/null/null.cc",
        "src/null/null_plugin.cc",
        "src/null/null_vm.cc",
        "src/v8/v8.cc",
        "src/wasm.cc",
    ],
    copts = ["-std=c++14"],
    deps = [
        ":include",
        "//external:abseil_flat_hash_map",
        "//external:abseil_optional",
        "//external:abseil_strings",
        "//external:protobuf",
        "//external:wee8",
        "//external:zlib",
        "@boringssl//:ssl",
        "@proxy_wasm_cpp_sdk//:api_lib",
        "@proxy_wasm_cpp_sdk//:common_lib",
    ],
)

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lib",
    hdrs = [
        "include/wasm.h",
    ],
    defines = ["ENVOY_WASM_WAVM"],
    include_prefix = "wasmtime",
    deps = [
        "@com_github_wasmtime//:rust_c_api",
    ],
)

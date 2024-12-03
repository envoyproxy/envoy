load("@rules_rust//rust:defs.bzl", "rust_static_library")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "helpers_lib",
    srcs = [
        "crates/runtime/src/helpers.c",
    ],
    visibility = ["//visibility:private"],
)

# TODO(keith): This should be using rust_library https://github.com/bazelbuild/rules_rust/issues/1238
rust_static_library(
    name = "rust_c_api",
    srcs = glob(["crates/c-api/src/**/*.rs"]),
    crate_root = "crates/c-api/src/lib.rs",
    edition = "2018",
    proc_macro_deps = [
        "@proxy_wasm_cpp_host//bazel/cargo/wasmtime/remote:wasmtime-c-api-macros",
    ],
    deps = [
        ":helpers_lib",
        "@proxy_wasm_cpp_host//bazel/cargo/wasmtime/remote:anyhow",
        "@proxy_wasm_cpp_host//bazel/cargo/wasmtime/remote:env_logger",
        "@proxy_wasm_cpp_host//bazel/cargo/wasmtime/remote:once_cell",
        "@proxy_wasm_cpp_host//bazel/cargo/wasmtime/remote:wasmtime",
    ],
)

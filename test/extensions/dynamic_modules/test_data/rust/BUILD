load("@rules_rust//rust:defs.bzl", "rust_shared_library")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//test/extensions/dynamic_modules:__pkg__"])

rust_shared_library(
    name = "no_op",
    srcs = ["no_op.rs"],
    edition = "2021",
    deps = [
        "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
    ],
)

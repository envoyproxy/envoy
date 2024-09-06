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

rust_shared_library(
    name = "no_program_init",
    srcs = ["no_program_init.rs"],
    edition = "2021",
)

rust_shared_library(
    name = "program_init_fail",
    srcs = ["program_init_fail.rs"],
    edition = "2021",
)

rust_shared_library(
    name = "abi_version_mismatch",
    srcs = ["abi_version_mismatch.rs"],
    edition = "2021",
)

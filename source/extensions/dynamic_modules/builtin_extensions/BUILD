load("@envoy_rust_crate_index//:defs.bzl", "all_crate_deps")
load("@rules_rust//rust:defs.bzl", "rust_clippy", "rust_static_library", "rustfmt_test")
load("//bazel:envoy_build_system.bzl", "envoy_extension_package")
load("//source/extensions/dynamic_modules:dynamic_modules.bzl", "envoy_dynamic_module_prefix_symbols")

licenses(["notice"])  # Apache 2

envoy_extension_package()

exports_files(["Cargo.toml"])

rust_static_library(
    name = "builtin_extensions_static_lib",
    srcs = glob(["*.rs"]),
    crate_root = "lib.rs",
    edition = "2021",
    deps = [
        "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
    ] + all_crate_deps(
        normal = True,
    ),
)

envoy_dynamic_module_prefix_symbols(
    name = "hickory_dns_static",
    archive = ":builtin_extensions_static_lib",
    module_name = "hickory_dns_static",
    visibility = [
        "//source/extensions/network/dns_resolver/hickory:__pkg__",
    ],
)

rustfmt_test(
    name = "fmt_builtin_extensions",
    testonly = True,
    tags = ["nocoverage"],
    targets = [":builtin_extensions_static_lib"],
)

rust_clippy(
    name = "clippy_builtin_extensions",
    testonly = True,
    tags = ["nocoverage"],
    deps = [":builtin_extensions_static_lib"],
)

load("@envoy_rust_crate_index//:defs.bzl", "all_crate_deps")
load("@rules_rust//rust:defs.bzl", "rust_clippy", "rust_static_library", "rustfmt_test")
load("//bazel:envoy_build_system.bzl", "envoy_extension_package")
load("//source/extensions/dynamic_modules:dynamic_modules.bzl", "envoy_dynamic_module_prefix_symbols")

licenses(["notice"])  # Apache 2

envoy_extension_package()

exports_files(["Cargo.toml"])

rust_static_library(
    name = "rustls_ktls_static_lib",
    srcs = ["rustls_ktls.rs"],
    edition = "2021",
    deps = [
        "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
    ] + all_crate_deps(
        normal = True,
    ),
)

envoy_dynamic_module_prefix_symbols(
    name = "rustls_ktls_static",
    archive = ":rustls_ktls_static_lib",
    module_name = "rustls_ktls_static",
    visibility = [
        "//source/extensions/transport_sockets/rustls:__pkg__",
    ],
)

rustfmt_test(
    name = "fmt_rustls_ktls",
    testonly = True,
    tags = ["nocoverage"],
    targets = [":rustls_ktls_static_lib"],
)

rust_clippy(
    name = "clippy_rustls_ktls",
    testonly = True,
    tags = ["nocoverage"],
    deps = [":rustls_ktls_static_lib"],
)

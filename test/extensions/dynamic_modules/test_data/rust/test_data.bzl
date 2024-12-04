load("@rules_rust//rust:defs.bzl", "rust_shared_library", "rustfmt_test")

def test_program(name):
    rust_shared_library(
        name = name,
        srcs = [name + ".rs"],
        edition = "2021",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
        ],
    )

    # As per the discussion in https://github.com/envoyproxy/envoy/pull/35627,
    # we set the rust_fmt and clippy target here instead of the part of //tools/code_format target for now.
    rustfmt_test(
        name = "rust_sdk_fmt" + name,
        tags = ["nocoverage"],
        targets = ["//test/extensions/dynamic_modules/test_data/rust:" + name],
    )

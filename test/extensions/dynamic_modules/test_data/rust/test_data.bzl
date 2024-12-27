load("@rules_rust//rust:defs.bzl", "rust_shared_library", "rust_test", "rustfmt_test")

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
        name = "rust_fmt_" + name,
        tags = ["nocoverage"],
        targets = ["//test/extensions/dynamic_modules/test_data/rust:" + name],
    )

    if name + "_test.rs" not in native.glob(["*.rs"]):
        return

    rust_test(
        name = "rust_test_" + name,
        srcs = [name + ".rs"],
        compile_data = [name + "_test.rs"],
        edition = "2021",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk_for_testing",
        ],
        tags = [
            # It is a known issue that TSAN detectes a false positive in the test runner of Rust toolchain:
            # https://github.com/rust-lang/rust/issues/39608
            # To avoid this issue, we need to use nightly and pass RUSTFLAGS="-Zsanitizer=thread" to this target only
            # when we run the test with TSAN: https://github.com/rust-lang/rust/commit/4b91729df22015bd412f6fc0fa397785d1e2159c
            # However, that causes symbol conflicts between the sanitizer built by Rust and the one built by Bazel.
            # Moreover, we also need to rebuild the Rust std-lib with the cargo option "-Zbuild-std", but that is
            # not supported by the rules_rust yet: https://github.com/bazelbuild/rules_rust/issues/2068
            # So, we disable TSAN for now. In contrast, ASAN works without any issue.
            "no_tsan",
            "nocoverage",
        ],
    )

load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@rules_rust//rust:defs.bzl", "rust_clippy", "rust_shared_library", "rust_static_library", "rust_test")
load("//source/extensions/dynamic_modules:dynamic_modules.bzl", "envoy_dynamic_module_prefix_symbols")

def test_program(name):
    srcs = [name + ".rs"]
    if name + "_test.rs" in native.glob(["*.rs"]):
        srcs = srcs + [name + "_test.rs"]

    _name = "_" + name
    rust_shared_library(
        name = _name,
        srcs = srcs,
        edition = "2021",
        crate_root = name + ".rs",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
        ],
        rustc_flags = ["-C", "link-args=-Wl,-undefined,dynamic_lookup"],
    )

    _static_name = name + "_static"
    _static_lib_name = name + "_static_lib"

    rust_static_library(
        name = _static_lib_name,
        srcs = srcs,
        edition = "2021",
        crate_root = name + ".rs",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
        ],
        rustc_flags = ["-C", "link-args=-Wl,-undefined,dynamic_lookup"],
    )

    envoy_dynamic_module_prefix_symbols(
        name = _static_name,
        module_name = _static_name,
        archive = ":" + _static_lib_name,
    )

    rust_clippy(
        name = "clippy_" + name,
        tags = ["nocoverage"],
        deps = [":" + _name],
        testonly = True,
    )

    rust_test(
        name = "test_" + name,
        srcs = srcs,
        crate_root = name + ".rs",
        edition = "2021",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
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

    # Copy the shared library to the expected name especially for MacOS which
    # defaults to lib<name>.dylib.
    copy_file(
        name = name,
        src = _name,
        out = "lib{}.so".format(name),
    )

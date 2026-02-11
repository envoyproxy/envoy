load(
    "@rules_rust//rust:defs.bzl",
    "rust_clippy",
    "rust_doc_test",
    "rust_test",
    "rustfmt_test",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dynamic_modules_test",
    srcs = ["dynamic_modules_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:abi_version_mismatch",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_program_init",
        "//test/extensions/dynamic_modules/test_data/c:program_child",
        "//test/extensions/dynamic_modules/test_data/c:program_global",
        "//test/extensions/dynamic_modules/test_data/c:program_init_assert",
        "//test/extensions/dynamic_modules/test_data/c:program_init_fail",
        "//test/extensions/dynamic_modules/test_data/rust:abi_version_mismatch",
        "//test/extensions/dynamic_modules/test_data/rust:no_op",
        "//test/extensions/dynamic_modules/test_data/rust:no_program_init",
        "//test/extensions/dynamic_modules/test_data/rust:program_init_fail",
    ],
    # This is to test dlopen with the current working directory (rundir in Bazel).
    env = {"LD_LIBRARY_PATH": "."},
    rbe_pool = "6gig",
    deps = [
        ":util",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    deps = [
        "//source/extensions/dynamic_modules:abi_impl",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = [
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

# We have targets for the tests of SDK itself here so that //test/... will be able to run them.
rust_test(
    name = "rust_sdk_test",
    crate = "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
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

rust_doc_test(
    name = "rust_sdk_doc_test",
    crate = "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
    tags = [
        "no_san",
        "nocoverage",
    ],
)

# As per the discussion in https://github.com/envoyproxy/envoy/pull/35627,
# we set the rust_fmt and clippy target here instead of the part of //tools/code_format target for now.
rustfmt_test(
    name = "rust_sdk_fmt",
    tags = ["nocoverage"],
    targets = [
        "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
        "//source/extensions/dynamic_modules/sdk/rust:build_script_",
    ],
)

rust_clippy(
    name = "rust_sdk_clippy",
    tags = ["nocoverage"],
    deps = ["//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk"],
)

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
        "//test/extensions/dynamic_modules/test_data/c:program_init_fail",
        "//test/extensions/dynamic_modules/test_data/rust:abi_version_mismatch",
        "//test/extensions/dynamic_modules/test_data/rust:no_op",
        "//test/extensions/dynamic_modules/test_data/rust:no_program_init",
        "//test/extensions/dynamic_modules/test_data/rust:program_init_fail",
    ],
    rbe_pool = "6gig",
    deps = [
        ":util",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_test(
    name = "abi_version_test",
    srcs = ["abi_version_test.cc"],
    data = [
        "//source/extensions/dynamic_modules:abi.h",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/extensions/dynamic_modules:abi_version_lib",
        "//test/test_common:environment_lib",
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
    # tags = ["nocoverage"],
)

rust_doc_test(
    name = "rust_sdk_doc_test",
    crate = "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
    tags = ["nocoverage"],
)

# As per the discussion in https://github.com/envoyproxy/envoy/pull/35627,
# we set the rust_fmt and clippy target here instead of the part of //tools/code_format target for now.
rustfmt_test(
    name = "rust_sdk_fmt",
    tags = ["nocoverage"],
    targets = ["//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk"],
)

rust_clippy(
    name = "rust_sdk_clippy",
    tags = ["nocoverage"],
    deps = ["//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk"],
)

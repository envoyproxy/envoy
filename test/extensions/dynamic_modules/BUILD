load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
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
    rbe_pool = "2core",
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_version_test",
    srcs = ["abi_version_test.cc"],
    data = [
        "//source/extensions/dynamic_modules:abi.h",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/extensions/dynamic_modules:abi_version_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

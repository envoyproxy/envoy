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
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/go:no_op",
    ],
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

sh_test(
    name = "ensure_Go_SDK_identical_abi_header",
    srcs = ["ensure_identical_abi_headers.sh"],
    args = [
        "$(location //source/extensions/dynamic_modules:abi.h)",
        "$(location //source/extensions/dynamic_modules/sdk/go:abi.h)",
    ],
    data = [
        "//source/extensions/dynamic_modules:abi.h",
        "//source/extensions/dynamic_modules/sdk/go:abi.h",
    ],
)

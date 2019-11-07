licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "utility_test",
    srcs = [
        "utility_test.cc",
    ],
    extension_name = "envoy.extensions.common.crypto.utility_lib",
    external_deps = [
        "ssl",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/extensions/common/crypto:utility_lib",
    ],
)

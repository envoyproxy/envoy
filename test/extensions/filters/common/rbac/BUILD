licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "matchers_test",
    srcs = ["matchers_test.cc"],
    deps = [
        "//source/extensions/filters/common/rbac:matchers_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "engine_impl_test",
    srcs = ["engine_impl_test.cc"],
    deps = [
        "//source/extensions/filters/common/rbac:engine_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_mock(
    name = "engine_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/filters/common/rbac:engine_interface",
    ],
)

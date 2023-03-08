load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "dso_test",
    srcs = ["dso_test.cc"],
    data = [
        "//contrib/golang/common/dso/test/test_data:simple.so",
    ],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_mock(
    name = "dso_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
    ],
)

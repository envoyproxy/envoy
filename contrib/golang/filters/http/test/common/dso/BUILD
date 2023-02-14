load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "dso_test",
    srcs = ["dso_test.cc"],
    data = [
        "//contrib/golang/filters/http/test/common/dso/test_data:simple.so",
    ],
    deps = [
        "//contrib/golang/filters/http/source/common/dso:dso_lib",
        "//test/test_common:utility_lib",
    ],
)

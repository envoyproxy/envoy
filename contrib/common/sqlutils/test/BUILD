load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "sqlutils_tests",
    srcs = [
        "sqlutils_test.cc",
    ],
    deps = [
        "//contrib/common/sqlutils/source:sqlutils_lib",
        "@sql-parser//:sqlparser",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "sqlutils_tests",
    srcs = [
        "sqlutils_test.cc",
    ],
    external_deps = ["sqlparser"],
    deps = [
        "//source/extensions/common/sqlutils:sqlutils_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "callback_test",
    srcs = ["callback_test.cc"],
    deps = [
        "//source/common/callback",
    ],
)

envoy_cc_test(
    name = "manager_test",
    srcs = ["manager_test.cc"],
    deps = [
        "//source/common/callback:manager",
    ],
)

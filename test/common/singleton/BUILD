licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "manager_impl_test",
    srcs = ["manager_impl_test.cc"],
    deps = [
        "//source/common/singleton:manager_impl_lib",
    ],
)

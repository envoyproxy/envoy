licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "secret_manager_impl_test",
    srcs = ["secret_manager_impl_test.cc"],
    data = ["//test/config/integration/certs"],
    deps = [
        "//source/common/secret:secret_impl_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

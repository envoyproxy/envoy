load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "synthetic_address_impl_test",
    srcs = ["synthetic_address_impl_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/network:synthetic_address_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "local_info_impl_test",
    srcs = ["local_info_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/local_info:local_info_lib",
        "//test/common/stats:stat_test_utility_lib",
    ],
)

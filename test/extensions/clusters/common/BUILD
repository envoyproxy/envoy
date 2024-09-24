load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "logical_host_test",
    srcs = ["logical_host_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/clusters/common:logical_host_lib",
        "//test/mocks/upstream:host_mocks",
    ],
)

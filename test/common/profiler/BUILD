load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "profiler_test",
    srcs = ["profiler_test.cc"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/profiler:profiler_lib",
        "//test/test_common:status_utility_lib",
    ],
)

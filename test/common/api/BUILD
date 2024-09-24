load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "os_sys_calls_test",
    srcs = ["os_sys_calls_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/api:os_sys_calls_lib",
    ],
)

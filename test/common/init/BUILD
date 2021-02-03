load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "watcher_impl_test",
    srcs = ["watcher_impl_test.cc"],
    deps = [
        "//test/mocks/init:init_mocks",
    ],
)

envoy_cc_test(
    name = "target_impl_test",
    srcs = ["target_impl_test.cc"],
    deps = [
        "//test/mocks/init:init_mocks",
    ],
)

envoy_cc_test(
    name = "manager_impl_test",
    srcs = ["manager_impl_test.cc"],
    deps = [
        "//source/common/init:manager_lib",
        "//test/mocks/init:init_mocks",
    ],
)

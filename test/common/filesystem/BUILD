load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filesystem_impl_test",
    srcs = ["filesystem_impl_test.cc"],
    deps = [
        "//source/common/filesystem:filesystem_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "directory_test",
    srcs = ["directory_test.cc"],
    deps = [
        "//source/common/filesystem:directory_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "watcher_impl_test",
    srcs = ["watcher_impl_test.cc"],
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/filesystem:watcher_lib",
        "//test/test_common:environment_lib",
    ],
)

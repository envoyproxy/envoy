licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "filesystem_impl_test",
    srcs = ["filesystem_impl_test.cc"],
    deps = [
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "watcher_impl_test",
    srcs = ["watcher_impl_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/filesystem:watcher_lib",
        "//test/test_common:environment_lib",
    ],
)

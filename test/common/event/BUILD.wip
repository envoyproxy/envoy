package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

envoy_cc_test(
    name = "dispatcher_impl_test",
    srcs = ["dispatcher_impl_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/event:dispatcher_lib_includes",
        "//test/mocks:common_lib",
    ],
)

envoy_cc_test(
    name = "file_event_impl_test",
    srcs = ["file_event_impl_test.cc"],
    deps = [
        "//include/envoy/event:file_event_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/event:dispatcher_lib_includes",
        "//test/mocks:common_lib",
    ],
)

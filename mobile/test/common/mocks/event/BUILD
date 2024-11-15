load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_mobile_package",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_mock(
    name = "event_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    repository = "@envoy",
    deps = [
        "//library/common/event:provisional_dispatcher_lib",
        "@envoy//envoy/event:deferred_deletable",
        "@envoy//envoy/event:dispatcher_interface",
        "@envoy//test/test_common:test_time_lib",
    ],
)

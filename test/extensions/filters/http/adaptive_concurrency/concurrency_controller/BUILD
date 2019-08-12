licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "gradient_controller_test",
    srcs = ["gradient_controller_test.cc"],
    extension_name = "envoy.filters.http.adaptive_concurrency.concurrency_controller",
    deps = [
        "//test/mocks/event:event_mocks",
        "//source/extensions/filters/http/adaptive_concurrency/concurrency_controller:concurrency_controller_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

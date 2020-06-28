load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "gradient_controller_test",
    srcs = ["gradient_controller_test.cc"],
    extension_name = "envoy.filters.http.adaptive_concurrency",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/http/adaptive_concurrency:adaptive_concurrency_filter_lib",
        "//source/extensions/filters/http/adaptive_concurrency/controller:controller_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/adaptive_concurrency/v3:pkg_cc_proto",
    ],
)

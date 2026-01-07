load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "async_example_integration_test",
    srcs = ["async_example_integration_test.cc"],
    deps = [
        "//source/extensions/filters/http/async_example:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "async_example_test",
    srcs = ["async_example_test.cc"],
    deps = [
        "//source/extensions/filters/http/async_example:async_example_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dispatcher_integration_test",
    srcs = ["dispatcher_integration_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/filters/http/local_error:config",
        "//library/common/extensions/filters/http/local_error:filter_cc_proto",
        "//library/common/http:dispatcher_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//test/common/http:common_lib",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/server:utility_lib",
    ],
)

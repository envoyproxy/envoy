load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "@envoy//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "platform_bridge_filter_test",
    srcs = ["platform_bridge_filter_test.cc"],
    extension_name = "envoy.filters.http.platform_bridge",
    repository = "@envoy",
    deps = [
        "//library/common/api:external_api_lib",
        "//library/common/extensions/filters/http/platform_bridge:config",
        "//library/common/extensions/filters/http/platform_bridge:filter_cc_proto",
        "@envoy//test/mocks/event:event_mocks",
        "@envoy//test/mocks/http:http_mocks",
        "@envoy//test/test_common:utility_lib",
    ],
)

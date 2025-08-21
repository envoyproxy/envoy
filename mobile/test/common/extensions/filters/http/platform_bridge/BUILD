load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load(
    "@envoy//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_extension_cc_test(
    name = "platform_bridge_filter_test",
    srcs = ["platform_bridge_filter_test.cc"],
    extension_names = ["envoy.filters.http.platform_bridge"],
    repository = "@envoy",
    deps = [
        "//library/common/api:external_api_lib",
        "//library/common/bridge:utility_lib",
        "//library/common/extensions/filters/http/platform_bridge:config",
        "//library/common/extensions/filters/http/platform_bridge:filter_cc_proto",
        "@envoy//test/mocks/event:event_mocks",
        "@envoy//test/mocks/http:http_mocks",
        "@envoy//test/mocks/server:factory_context_mocks",
        "@envoy//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "platform_bridge_filter_integration_test",
    srcs = [
        "platform_bridge_filter_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.platform_bridge"],
    repository = "@envoy",
    deps = [
        "//library/common/api:external_api_lib",
        "//library/common/extensions/filters/http/platform_bridge:config",
        "//library/common/extensions/filters/http/platform_bridge:filter_cc_proto",
        "@envoy//test/integration:http_integration_lib_light",
        "@envoy//test/mocks/server:factory_context_mocks",
        "@envoy//test/test_common:simulated_time_system_lib",
        "@envoy//test/test_common:utility_lib",
    ],
)

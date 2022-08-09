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
    name = "config_test",
    size = "small",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.custom_response"],
    deps = [
        "//source/common/http:message_lib",
        "//source/common/network/matching:inputs_lib",
        "//source/extensions/filters/http/custom_response:config",
        "//source/extensions/filters/http/custom_response:custom_response_filter",
        "//source/extensions/filters/http/custom_response:factory",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "custom_response_filter_test",
    size = "small",
    srcs = ["custom_response_filter_test.cc"],
    extension_names = ["envoy.filters.http.custom_response"],
    deps = [
        "//source/common/http:message_lib",
        "//source/common/network/matching:inputs_lib",
        "//source/extensions/filters/http/custom_response:config",
        "//source/extensions/filters/http/custom_response:custom_response_filter",
        "//source/extensions/filters/http/custom_response:factory",
        "//test/common/http:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)

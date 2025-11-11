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
    name = "sampling_decision_filter_test",
    srcs = ["sampling_decision_filter_test.cc"],
    extension_names = ["envoy.filters.http.sampling_decision"],
    deps = [
        "//source/extensions/filters/http/sampling_decision:sampling_decision_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/sampling_decision/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "sampling_decision_integration_test",
    srcs = ["sampling_decision_integration_test.cc"],
    extension_names = ["envoy.filters.http.sampling_decision"],
    deps = [
        "//source/extensions/filters/http/sampling_decision:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/sampling_decision/v3:pkg_cc_proto",
    ],
)

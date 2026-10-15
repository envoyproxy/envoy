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
    name = "a2a_json_parser_test",
    srcs = ["a2a_json_parser_test.cc"],
    extension_names = ["envoy.filters.http.a2a"],
    deps = [
        "//source/extensions/filters/http/a2a:a2a_json_parser_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "a2a_filter_test",
    srcs = ["a2a_filter_test.cc"],
    extension_names = ["envoy.filters.http.a2a"],
    deps = [
        "//source/extensions/filters/http/a2a:a2a_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/a2a/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.a2a"],
    deps = [
        "//source/extensions/filters/http/a2a:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "a2a_filter_integration_test",
    srcs = ["a2a_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.a2a"],
    deps = [
        "//source/extensions/filters/http/a2a:config",
        "//test/integration:fake_access_log_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/a2a/v3:pkg_cc_proto",
    ],
)

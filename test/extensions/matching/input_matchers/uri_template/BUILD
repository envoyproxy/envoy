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
    srcs = ["config_test.cc"],
    extension_names = ["envoy.matching.matchers.uri_template"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/matching/input_matchers/uri_template:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/matching/input_matchers/uri_template/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    extension_names = ["envoy.matching.matchers.uri_template"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/matching/input_matchers/uri_template:matcher_lib",
        "@re2",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.matching.matchers.uri_template"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/match_delegate:config",
        "//source/extensions/matching/input_matchers/uri_template:config",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:set_response_code_filter_lib",
    ],
)

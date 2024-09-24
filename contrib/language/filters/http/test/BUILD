load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "language_config_test",
    srcs = ["language_config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/language/filters/http/source:config_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/language/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "language_integration_test",
    srcs = ["language_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/language/filters/http/source:config_lib",
        "//test/integration:http_protocol_integration_lib",
    ],
)

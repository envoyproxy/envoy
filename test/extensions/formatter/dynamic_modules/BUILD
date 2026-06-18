load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:formatter_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:formatter_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:formatter_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:formatter_missing_format",
        "//test/extensions/dynamic_modules/test_data/c:formatter_missing_parse",
        "//test/extensions/dynamic_modules/test_data/c:formatter_missing_provider_destroy",
        "//test/extensions/dynamic_modules/test_data/c:formatter_no_op",
        "//test/extensions/dynamic_modules/test_data/rust:formatter_integration_test",
    ],
    deps = [
        "//source/common/formatter:substitution_format_string_lib",
        "//source/extensions/formatter/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/formatter/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:formatter_integration_test",
    ],
    deps = [
        "//source/extensions/formatter/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

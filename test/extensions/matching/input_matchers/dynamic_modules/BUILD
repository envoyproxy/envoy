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
        "//test/extensions/dynamic_modules/test_data/c:matcher_check_headers",
        "//test/extensions/dynamic_modules/test_data/c:matcher_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:matcher_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:matcher_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:matcher_missing_match",
        "//test/extensions/dynamic_modules/test_data/c:matcher_no_op",
    ],
    deps = [
        "//envoy/registry",
        "//source/extensions/matching/input_matchers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:matcher_check_headers",
        "//test/extensions/dynamic_modules/test_data/c:matcher_no_op",
    ],
    deps = [
        "//source/extensions/matching/input_matchers/dynamic_modules:matcher_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    deps = [
        "//source/extensions/matching/input_matchers/dynamic_modules:matcher_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:matcher_check_headers",
    ],
    deps = [
        "//source/extensions/matching/http/dynamic_modules:data_input_lib",
        "//source/extensions/matching/input_matchers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/matching/http/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/input_matchers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

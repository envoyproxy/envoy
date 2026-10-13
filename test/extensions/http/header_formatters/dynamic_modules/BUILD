load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_dyn_module_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_dyn_module_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_destroy",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_format",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_new",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_process_key",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_no_op",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_preserve_case",
    ],
    deps = [
        "//envoy/registry",
        "//source/extensions/http/header_formatters/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/header_formatters/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_dyn_module_test(
    name = "header_formatter_test",
    srcs = ["header_formatter_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_destroy",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_format",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_new",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_missing_process_key",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_no_op",
        "//test/extensions/dynamic_modules/test_data/c:header_formatter_preserve_case",
    ],
    deps = [
        "//source/extensions/http/header_formatters/dynamic_modules:header_formatter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/event:event_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_dyn_module_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/cpp:header_formatter_integration_test",
        "//test/extensions/dynamic_modules/test_data/go:header_formatter_integration_test",
        "//test/extensions/dynamic_modules/test_data/rust:header_formatter_integration_test",
    ],
    env = {"GODEBUG": "cgocheck=0"},
    deps = [
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        # The C++ SDK compiles every extension point into one archive, so the test binary must
        # export the callbacks of the other dynamic module extensions the SDK references.
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/filters/http/dynamic_modules:abi_impl",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//source/extensions/filters/network/dynamic_modules:config",
        "//source/extensions/http/early_header_mutation/dynamic_modules:config",
        "//source/extensions/http/header_formatters/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/header_formatters/dynamic_modules/v3:pkg_cc_proto",
    ],
)

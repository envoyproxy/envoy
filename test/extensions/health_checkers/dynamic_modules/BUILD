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
    data = [
        "//test/extensions/dynamic_modules/test_data/c:health_checker_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:health_checker_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:health_checker_missing_session_destroy",
        "//test/extensions/dynamic_modules/test_data/c:health_checker_missing_session_new",
        "//test/extensions/dynamic_modules/test_data/c:health_checker_missing_session_on_interval",
        "//test/extensions/dynamic_modules/test_data/c:health_checker_no_op",
        "//test/extensions/dynamic_modules/test_data/rust:health_checker_integration_test",
    ],
    extension_names = ["envoy.health_checkers.dynamic_modules"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/registry",
        "//source/common/upstream:health_checker_lib",
        "//source/extensions/health_checkers/dynamic_modules:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "health_checker_test",
    srcs = ["health_checker_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:health_checker_session_new_null",
        "//test/extensions/dynamic_modules/test_data/rust:health_checker_integration_test",
    ],
    extension_names = ["envoy.health_checkers.dynamic_modules"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/api:api_lib",
        "//source/common/config:metadata_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/health_checkers/dynamic_modules:health_checker_lib",
        "//test/common/upstream:utility_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks:common_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:health_check_event_logger_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    extension_names = ["envoy.health_checkers.dynamic_modules"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/health_checkers/dynamic_modules:health_checker_lib",
        "//test/test_common:utility_lib",
    ],
)

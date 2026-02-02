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
        "//test/extensions/dynamic_modules/test_data/c:access_log_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:access_log_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:access_log_missing_logger_destroy",
        "//test/extensions/dynamic_modules/test_data/c:access_log_missing_logger_log",
        "//test/extensions/dynamic_modules/test_data/c:access_log_missing_logger_new",
        "//test/extensions/dynamic_modules/test_data/c:access_log_no_op",
    ],
    deps = [
        "//envoy/registry",
        "//source/extensions/access_loggers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "access_log_test",
    srcs = ["access_log_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:access_log_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:access_log_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:access_log_no_op",
    ],
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/access_loggers/dynamic_modules:access_log_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    deps = [
        "//source/extensions/access_loggers/dynamic_modules:access_log_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:access_log_integration_test",
    ],
    deps = [
        "//source/extensions/access_loggers/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/access_loggers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

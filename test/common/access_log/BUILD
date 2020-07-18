load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "access_log_impl_test",
    srcs = ["access_log_impl_test.cc"],
    deps = [
        "//source/common/access_log:access_log_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/access_loggers/grpc:http_config",
        "//source/extensions/access_loggers/grpc:tcp_config",
        "//test/common/stream_info:test_util",
        "//test/common/upstream:utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "access_log_manager_impl_test",
    srcs = ["access_log_manager_impl_test.cc"],
    deps = [
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/stats:stats_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
    ],
)

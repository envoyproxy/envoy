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
    name = "access_log_impl_test",
    srcs = ["access_log_impl_test.cc"],
    extension_names = ["envoy.access_loggers.extension_filters.process_ratelimit"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/access_log:access_log_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/stream_info:utility_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/access_loggers/filters/process_ratelimit:config",
        "//source/extensions/config_subscription/filesystem:filesystem_subscription_lib",
        "//test/common/stream_info:test_util",
        "//test/common/upstream:utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.access_loggers.extension_filters.process_ratelimit"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/access_loggers/filters/process_ratelimit:config",
        "//source/extensions/formatter/cel:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/filters/process_ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

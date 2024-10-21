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
    name = "fluentd_access_log_impl_test",
    srcs = ["fluentd_access_log_impl_test.cc"],
    extension_names = ["envoy.access_loggers.fluentd"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/access_loggers/fluentd:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@com_github_msgpack_cpp//:msgpack",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/fluentd/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "substitution_formatter_test",
    srcs = ["substitution_formatter_test.cc"],
    extension_names = ["envoy.access_loggers.fluentd"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/access_loggers/fluentd:substitution_formatter_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "fluentd_access_log_integration_test",
    size = "large",
    srcs = ["fluentd_access_log_integration_test.cc"],
    extension_names = ["envoy.access_loggers.fluentd"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/access_loggers/fluentd:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/formatter/cel:config",
        "//test/integration:integration_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/fluentd/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

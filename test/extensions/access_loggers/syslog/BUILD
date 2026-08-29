load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "syslog_access_log_impl_test",
    srcs = ["syslog_access_log_impl_test.cc"],
    deps = [
        "//source/common/api:api_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_lib",
        "//source/extensions/access_loggers/syslog:syslog_access_log_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

envoy_cc_test(
    name = "formatter_test",
    srcs = ["formatter_test.cc"],
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/extensions/access_loggers/syslog:syslog_access_log_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//envoy/common:exception_lib",
        "//source/extensions/access_loggers/syslog:config",
        "//test/mocks/network:transport_socket_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings:string_view",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/syslog/v3:pkg_cc_proto",
    ],
)

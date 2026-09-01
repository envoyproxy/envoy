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
    extension_names = ["envoy.access_loggers.syslog"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/common:exception_lib",
        "//source/extensions/access_loggers/syslog:config",
        "//test/mocks/api:api_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/access_loggers/syslog/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "syslog_access_log_test",
    srcs = ["syslog_access_log_test.cc"],
    extension_names = ["envoy.access_loggers.syslog"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/access_log:access_log_lib",
        "//source/common/api:api_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_lib",
        "//source/extensions/access_loggers/syslog:config",
        "//source/extensions/access_loggers/syslog:syslog_access_log_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/syslog/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "udp_sender_test",
    srcs = ["udp_sender_test.cc"],
    extension_names = ["envoy.access_loggers.syslog"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/api:api_lib",
        "//source/common/network:address_lib",
        "//source/common/network:io_socket_error_lib",
        "//source/common/network:socket_lib",
        "//source/extensions/access_loggers/syslog:syslog_access_log_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

envoy_extension_cc_test(
    name = "formatter_test",
    srcs = ["formatter_test.cc"],
    extension_names = ["envoy.access_loggers.syslog"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/extensions/access_loggers/syslog:syslog_access_log_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

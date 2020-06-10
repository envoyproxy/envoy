load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "admin_instance_lib",
    srcs = ["admin_instance.cc"],
    hdrs = ["admin_instance.h"],
    deps = [
        "//source/server/admin:admin_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
    ],
)

envoy_cc_test(
    name = "admin_test",
    srcs = ["admin_test.cc"],
    deps = [
        ":admin_instance_lib",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/http:message_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "admin_filter_test",
    srcs = ["admin_filter_test.cc"],
    deps = [
        "//source/server/admin:admin_filter_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "stats_handler_test",
    srcs = ["stats_handler_test.cc"],
    deps = [
        ":admin_instance_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server/admin:stats_handler_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "runtime_handler_test",
    srcs = ["runtime_handler_test.cc"],
    deps = [":admin_instance_lib"],
)

envoy_cc_test(
    name = "prometheus_stats_test",
    srcs = ["prometheus_stats_test.cc"],
    deps = [
        "//source/server/admin:prometheus_stats_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "logs_handler_test",
    srcs = ["logs_handler_test.cc"],
    deps = [
        ":admin_instance_lib",
    ],
)

envoy_cc_test(
    name = "profiling_handler_test",
    srcs = ["profiling_handler_test.cc"],
    deps = [
        ":admin_instance_lib",
        "//test/test_common:logging_lib",
    ],
)

envoy_cc_test(
    name = "server_info_handler_test",
    srcs = ["server_info_handler_test.cc"],
    deps = [
        ":admin_instance_lib",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_tracker_impl_test",
    srcs = ["config_tracker_impl_test.cc"],
    deps = [
        "//source/server/admin:config_tracker_lib",
        "//test/mocks:common_lib",
    ],
)

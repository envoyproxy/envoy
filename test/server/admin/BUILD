load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_admin_functionality",
    "envoy_select_admin_html",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "admin_html_util_test",
    srcs = ["admin_html_util_test.cc"],
    deps = [
        "//source/server/admin:admin_html_util",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "admin_factory_context_test",
    srcs = ["admin_factory_context_test.cc"],
    deps = [
        "//source/server/admin:admin_factory_context",
        "//test/mocks/server:instance_mocks",
    ],
)

envoy_cc_test_library(
    name = "admin_instance_lib",
    srcs = envoy_select_admin_functionality(["admin_instance.cc"]),
    hdrs = envoy_select_admin_functionality(["admin_instance.h"]),
    deps = [
        "//source/server:configuration_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "admin_test",
    srcs = envoy_select_admin_functionality(["admin_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "//envoy/json:json_object_interface",
        "//envoy/runtime:runtime_interface",
        "//source/common/http:message_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "admin_filter_test",
    srcs = envoy_select_admin_functionality(["admin_filter_test.cc"]),
    deps = [
        "//source/server/admin:admin_lib",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "admin_html_test",
    srcs = envoy_select_admin_html(["admin_html_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "//source/server/admin:admin_lib",
    ],
)

envoy_cc_test(
    name = "stats_handler_test",
    srcs = envoy_select_admin_functionality(["stats_handler_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "//source/common/common:regex_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/server/admin:utils_lib",
        "//test/mocks/server:admin_stream_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:real_threads_test_helper_lib",
        "//test/test_common:stats_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "stats_params_test",
    srcs = envoy_select_admin_functionality(["stats_params_test.cc"]),
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/server/admin:stats_params_lib",
    ],
)

envoy_cc_test(
    name = "stats_render_test",
    srcs = envoy_select_admin_functionality(["stats_render_test.cc"]) + envoy_select_admin_html([
        "stats_html_render_test.cc",
    ]),
    deps = [
        ":stats_render_test_base",
        "//source/server/admin:admin_lib",
    ],
)

envoy_cc_test_library(
    name = "stats_render_test_base",
    srcs = envoy_select_admin_functionality(["stats_render_test_base.cc"]),
    hdrs = ["stats_render_test_base.h"],
    deps = [
        "//source/common/stats:thread_local_store_lib",
        "//source/server/admin:stats_request_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "stats_request_test",
    srcs = envoy_select_admin_functionality(["stats_request_test.cc"]),
    deps = [
        "//source/common/stats:thread_local_store_lib",
        "//source/server/admin:stats_request_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:stats_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "stats_handler_speed_test",
    srcs = envoy_select_admin_functionality(["stats_handler_speed_test.cc"]),
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server/admin:admin_lib",
        "//test/common/stats:real_thread_test_base",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test(
    name = "utils_test",
    srcs = envoy_select_admin_functionality(["utils_test.cc"]),
    deps = [
        "//source/server/admin:utils_lib",
    ],
)

envoy_cc_test(
    name = "runtime_handler_test",
    srcs = envoy_select_admin_functionality(["runtime_handler_test.cc"]),
    deps = [":admin_instance_lib"],
)

envoy_cc_test(
    name = "prometheus_stats_test",
    srcs = envoy_select_admin_functionality(["prometheus_stats_test.cc"]),
    deps = [
        "//source/common/stats:tag_producer_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server/admin:prometheus_stats_lib",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:stats_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "logs_handler_test",
    srcs = envoy_select_admin_functionality(["logs_handler_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_test(
    name = "profiling_handler_test",
    srcs = envoy_select_admin_functionality(["profiling_handler_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "//test/test_common:logging_lib",
    ],
)

envoy_cc_test(
    name = "server_info_handler_test",
    srcs = envoy_select_admin_functionality(["server_info_handler_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "clusters_handler_test",
    srcs = envoy_select_admin_functionality(["clusters_handler_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_dump_handler_test",
    srcs = envoy_select_admin_functionality(["config_dump_handler_test.cc"]),
    deps = [
        ":admin_instance_lib",
        "//test/integration/filters:test_listener_filter_lib",
        "//test/integration/filters:test_network_filter_lib",
    ],
)

envoy_cc_test(
    name = "init_dump_handler_test",
    srcs = envoy_select_admin_functionality(["init_dump_handler_test.cc"]),
    deps = [
        ":admin_instance_lib",
    ],
)

envoy_cc_test(
    name = "config_tracker_impl_test",
    srcs = envoy_select_admin_functionality(["config_tracker_impl_test.cc"]),
    deps = [
        "//source/common/common:matchers_lib",
        "//source/server/admin:config_tracker_lib",
        "//test/mocks:common_lib",
    ],
)

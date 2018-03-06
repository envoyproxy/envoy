licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "cds_api_impl_test",
    srcs = ["cds_api_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:message_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/upstream:cds_api_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "cluster_manager_impl_test",
    srcs = ["cluster_manager_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/config:bootstrap_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/ssl:context_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/server/config/network:raw_buffer_socket_lib",
        "//source/server/config/network:ssl_socket_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "eds_test",
    srcs = ["eds_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/upstream:eds_lib",
        "//source/server/config/network:raw_buffer_socket_lib",
        "//source/server/config/network:ssl_socket_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:eds_cc",
    ],
)

envoy_cc_test(
    name = "health_checker_impl_test",
    srcs = ["health_checker_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/config:cds_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:health_checker_lib",
        "//source/common/upstream:upstream_lib",
        "//test/common/http:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/redis:redis_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "host_utility_test",
    srcs = ["host_utility_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:host_utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "load_balancer_impl_test",
    srcs = ["load_balancer_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "load_balancer_simulation_test",
    srcs = ["load_balancer_simulation_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "load_stats_reporter_test",
    srcs = ["load_stats_reporter_test.cc"],
    deps = [
        "//source/common/stats:stats_lib",
        "//source/common/upstream:load_stats_reporter_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:eds_cc",
        "@envoy_api//envoy/api/v2/endpoint:endpoint_cc",
        "@envoy_api//envoy/api/v2/endpoint:load_report_cc",
    ],
)

envoy_cc_test(
    name = "logical_dns_cluster_test",
    srcs = ["logical_dns_cluster_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:logical_dns_cluster_lib",
        "//source/common/upstream:upstream_lib",
        "//source/server/config/network:raw_buffer_socket_lib",
        "//source/server/config/network:ssl_socket_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "original_dst_cluster_test",
    srcs = ["original_dst_cluster_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:original_dst_cluster_lib",
        "//source/common/upstream:upstream_lib",
        "//source/server/config/network:raw_buffer_socket_lib",
        "//source/server/config/network:ssl_socket_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "outlier_detection_impl_test",
    srcs = ["outlier_detection_impl_test.cc"],
    external_deps = ["abseil_optional"],
    deps = [
        ":utility_lib",
        "//include/envoy/common:time_interface",
        "//source/common/network:utility_lib",
        "//source/common/upstream:outlier_detection_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "resource_manager_impl_test",
    srcs = ["resource_manager_impl_test.cc"],
    deps = [
        "//source/common/upstream:resource_manager_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)

envoy_cc_test(
    name = "ring_hash_lb_test",
    srcs = ["ring_hash_lb_test.cc"],
    deps = [
        ":utility_lib",
        "//include/envoy/router:router_interface",
        "//source/common/network:utility_lib",
        "//source/common/upstream:ring_hash_lb_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "maglev_lb_test",
    srcs = ["maglev_lb_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/upstream:maglev_lb_lib",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_binary(
    name = "load_balancer_benchmark",
    testonly = 1,
    srcs = ["load_balancer_benchmark.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/upstream:maglev_lb_lib",
        "//source/common/upstream:ring_hash_lb_lib",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_cc_test(
    name = "sds_test",
    srcs = ["sds_test.cc"],
    data = glob(["test_data/**"]),
    deps = [
        ":utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/http:message_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:eds_lib",
        "//source/server/config/network:raw_buffer_socket_lib",
        "//source/server/config/network:ssl_socket_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_test(
    name = "subset_lb_test",
    srcs = ["subset_lb_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:subset_lb_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "upstream_impl_test",
    srcs = ["upstream_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//include/envoy/api:api_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/config:metadata_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/server/config/network:raw_buffer_socket_lib",
        "//source/server/config/network:ssl_socket_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/config:cds_json_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
    ],
)

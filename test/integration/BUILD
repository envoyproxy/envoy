load("@rules_python//python:defs.bzl", "py_binary")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_admin_functionality",
    "envoy_select_enable_http3",
    "envoy_select_enable_yaml",
    "envoy_select_envoy_mobile_listener",
    "envoy_select_linkstatic",
    "envoy_sh_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "ads_integration_lib",
    srcs = [
        "ads_integration.cc",
    ],
    hdrs = [
        "ads_integration.h",
    ],
    data = [
        "//test/config/integration:server_xds_files",
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/common/common:matchers_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//source/common/version:version_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "ads_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality(
        ["ads_integration_test.cc"],
    ),
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":ads_integration_lib",
        ":http_integration_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "alpn_integration_test",
    size = "large",
    srcs = ["alpn_integration_test.cc"],
    rbe_pool = "2core",
    shard_count = 2,
    tags = [
        "cpu:3",
    ],
    deps = [":http_integration_lib"],
)

envoy_cc_test(
    name = "api_listener_integration_test",
    size = "large",
    srcs = ["api_listener_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//test/mocks/http:stream_encoder_mock",
        "//test/server:utility_lib",
    ],
)

py_binary(
    name = "capture_fuzz_gen",
    srcs = ["capture_fuzz_gen.py"],
    licenses = ["notice"],  # Apache 2
    visibility = ["//visibility:public"],
    deps = [
        ":capture_fuzz_proto_py_proto",
        "@envoy_api//envoy/data/tap/v2alpha:pkg_py_proto",
    ],
)

envoy_proto_library(
    name = "capture_fuzz_proto",
    srcs = [":capture_fuzz.proto"],
)

envoy_proto_library(
    name = "h2_capture_fuzz_proto",
    srcs = [":h2_capture_fuzz.proto"],
)

envoy_cc_test(
    name = "cds_integration_test",
    size = "large",
    srcs = ["cds_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    shard_count = 4,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "eds_integration_test",
    size = "large",
    srcs = ["eds_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//test/config:utility_lib",
        "//test/integration/filters:eds_ready_filter_config_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "leds_integration_test",
    size = "large",
    srcs = ["leds_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//test/config:utility_lib",
        "//test/test_common:network_utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "filter_manager_integration_proto",
    srcs = [":filter_manager_integration_test.proto"],
)

envoy_cc_test(
    name = "filter_manager_integration_test",
    size = "large",
    srcs = [
        "filter_manager_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":filter_manager_integration_proto_cc_proto",
        ":http_integration_lib",
        ":integration_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration/filters:test_listener_filter_lib",
        "//test/server:utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "upstream_socket_proto",
    srcs = [":upstream_socket.proto"],
    deps = [
        "@envoy_api//envoy/config/core/v3:pkg",
    ],
)

envoy_cc_test(
    name = "upstream_access_log_integration_test",
    size = "large",
    srcs = [
        "upstream_access_log_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        ":upstream_socket_proto_cc_proto",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cluster_filter_integration_test",
    size = "large",
    srcs = ["cluster_filter_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        "//envoy/network:filter_interface",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/config:utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "custom_cluster_integration_test",
    size = "large",
    srcs = ["custom_cluster_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//test/config:utility_lib",
        "//test/integration/clusters:custom_static_cluster",
        "//test/test_common:network_utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "vhds_integration_test",
    size = "large",
    srcs = ["vhds_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":vhds_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "vhds_lib",
    hdrs = ["vhds.h"],
    deps = [
        ":http_integration_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "drain_close_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "drain_close_integration_test.cc",
    ]),
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

exports_files(["test_utility.sh"])

envoy_cc_test_binary(
    name = "hotrestart_main",
    srcs = ["hotrestart_main.cc"],
    rbe_pool = "2core",
    deps = [
        ":common_extensions_lib",
        "//source/common/http:rds_lib",
        "//source/exe:envoy_main_common_with_core_extensions_lib",
        "//source/exe:platform_impl_lib",
        "//source/extensions/clusters/static:static_cluster_lib",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/load_balancing_policies/least_request:config",
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/ring_hash:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//source/extensions/transport_sockets/tls:config",
        "@com_google_absl//absl/debugging:symbolize",
    ],
)

envoy_cc_test_binary(
    name = "hotrestart_small_main",
    srcs = ["hotrestart_main.cc"],
    linkstatic = envoy_select_linkstatic(),
    rbe_pool = "2core",
    deps = [
        ":common_extensions_lib",
        "//source/exe:main_common_lib",
        "//source/exe:platform_impl_lib",
        "//source/exe:process_wide_lib",
        "//source/exe:stripped_main_base_lib",
        "//source/extensions/listener_managers/validation_listener_manager:validation_listener_manager_lib",
        "//source/extensions/transport_sockets/tls:config",
        "@com_google_absl//absl/debugging:symbolize",
    ],
)

envoy_sh_test(
    name = "hotrestart_test",
    size = "large",
    srcs = select({
        "//bazel:disable_hot_restart_or_admin": [],
        "//conditions:default": ["hotrestart_test.sh"],
    }),
    cc_binary = [":hotrestart_main"],
    coverage = False,
    data = [
        "test_utility.sh",
        "//test/config/integration:server_config_files",
        "//tools/socket_passing",
    ],
    # Hot restart does not apply on Windows, skipping
    tags = ["skip_on_windows"],
)

envoy_sh_test(
    name = "run_envoy_small_test",
    size = "large",
    srcs = ["run_envoy_test.sh"],
    cc_binary = [":hotrestart_small_main"],
    data = [
        "test_utility.sh",
        "//test/config/integration:server_config_files",
    ],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
)

envoy_cc_test(
    name = "alpn_selection_integration_test",
    size = "large",
    srcs = [
        "alpn_selection_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "header_integration_test",
    size = "large",
    srcs = [
        "header_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":http_protocol_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/protobuf",
        "//source/extensions/http/original_ip_detection/xff:config",
        "//test/config:v2_link_hacks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http_conn_pool_integration_test",
    size = "large",
    srcs = ["http_conn_pool_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "circuit_breakers_integration_test",
    size = "large",
    srcs = ["circuit_breakers_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http2_flood_integration_test",
    size = "large",
    srcs = [
        "http2_flood_integration_test.cc",
    ],
    rbe_pool = "2core",
    shard_count = 6,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":autonomous_upstream_lib",
        ":http_integration_lib",
        ":socket_interface_swap_lib",
        ":tracked_watermark_buffer_lib",
        "//test/common/http/http2:http2_frame",
        "//test/integration/filters:backpressure_filter_config_lib",
        "//test/integration/filters:set_response_code_filter_config_proto_cc_proto",
        "//test/integration/filters:set_response_code_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "multiplexed_integration_test",
    size = "large",
    srcs = [
        "multiplexed_integration_test.cc",
    ],
    rbe_pool = "2core",
    shard_count = 10,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        ":socket_interface_swap_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/load_balancing_policies/ring_hash:config",
        "//test/integration/filters:encode1xx_local_reply_config_lib",
        "//test/integration/filters:local_reply_during_decoding_filter_lib",
        "//test/integration/filters:metadata_control_filter_lib",
        "//test/integration/filters:metadata_stop_all_filter_config_lib",
        "//test/integration/filters:on_local_reply_filter_config_lib",
        "//test/integration/filters:request_metadata_filter_config_lib",
        "//test/integration/filters:response_metadata_filter_config_lib",
        "//test/integration/filters:set_response_code_filter_config_proto_cc_proto",
        "//test/integration/filters:set_response_code_filter_lib",
        "//test/integration/filters:stop_in_headers_continue_in_data_filter_lib",
        "//test/integration/filters:stop_iteration_and_continue",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:retry_priority_factory_mocks",
        "//test/mocks/upstream:retry_priority_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "shadow_policy_integration_test",
    size = "large",
    srcs = ["shadow_policy_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        ":socket_interface_swap_lib",
        "//test/integration/filters:add_header_filter_config_lib",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/integration/filters:on_local_reply_filter_config_lib",
        "//test/integration/filters:repick_cluster_filter_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/upstream_codec/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "upstream_http_filter_integration_test",
    size = "large",
    srcs = ["upstream_http_filter_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        "//test/integration/filters:add_header_filter_config_lib",
        "//test/integration/filters:add_header_filter_proto_cc_proto",
        "//test/integration/filters:on_local_reply_filter_config_lib",
        "//test/integration/filters:repick_cluster_filter_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "upstream_network_filter_integration_test",
    size = "large",
    srcs = ["upstream_network_filter_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration/filters:test_network_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/extension/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "buffer_accounting_integration_test",
    size = "large",
    srcs = [
        "buffer_accounting_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":base_overload_integration_test_lib",
        ":http_integration_lib",
        ":http_protocol_integration_lib",
        ":socket_interface_swap_lib",
        ":tracked_watermark_buffer_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration/filters:tee_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "socket_interface_swap_lib",
    srcs = [
        "socket_interface_swap.cc",
    ],
    hdrs = [
        "socket_interface_swap.h",
    ],
    deps = [
        "//test/integration/filters:test_socket_interface_lib",
        "@com_google_absl//absl/synchronization",
    ],
)

envoy_cc_test(
    name = "http_subset_lb_integration_test",
    size = "large",
    srcs = [
        "http_subset_lb_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/extensions/load_balancing_policies/maglev:config",
        "//source/extensions/load_balancing_policies/ring_hash:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/common/upstream:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "transport_socket_match_integration_test",
    size = "large",
    srcs = [
        "transport_socket_match_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/common/upstream:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "header_casing_integration_test",
    size = "large",
    srcs = [
        "header_casing_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http_typed_per_filter_config_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "http_typed_per_filter_config_test.cc",
    ]),
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//test/integration/filters:set_route_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "http_timeout_integration_test_lib",
    hdrs = ["http_timeout_integration_test.h"],
    deps = [
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http_timeout_integration_test",
    size = "large",
    srcs = [
        "http_timeout_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":http_timeout_integration_test_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "filter_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "filter_integration_test.cc",
    ]),
    rbe_pool = "2core",
    shard_count = 8,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        ":socket_interface_swap_lib",
        "//envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/buffer:config",
        "//test/integration/filters:add_body_filter_config_lib",
        "//test/integration/filters:add_encode_metadata_filter_lib",
        "//test/integration/filters:add_invalid_data_filter_lib",
        "//test/integration/filters:assert_non_reentrant_filter_lib",
        "//test/integration/filters:buffer_continue_filter_lib",
        "//test/integration/filters:continue_after_local_reply_filter_lib",
        "//test/integration/filters:continue_headers_only_inject_body",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/integration/filters:encoder_recreate_stream_filter_lib",
        "//test/integration/filters:invalid_header_filter_lib",
        "//test/integration/filters:local_reply_during_decoding_filter_lib",
        "//test/integration/filters:local_reply_during_encoding_data_filter_lib",
        "//test/integration/filters:local_reply_during_encoding_filter_lib",
        "//test/integration/filters:local_reply_with_metadata_filter_lib",
        "//test/integration/filters:metadata_control_filter_lib",
        "//test/integration/filters:non_terminal_encoding_filter_lib",
        "//test/integration/filters:on_local_reply_filter_config_lib",
        "//test/integration/filters:remove_response_headers_lib",
        "//test/integration/filters:reset_stream_filter_config_lib",
        "//test/integration/filters:stop_iteration_and_continue",
        "//test/integration/filters:stop_iteration_headers_inject_body",
        "//test/integration/filters:wait_for_whole_request_and_response_config_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "protocol_integration_test_lib",
    srcs = ["protocol_integration_test.cc"],
    hdrs = ["protocol_integration_test.h"],
    rbe_pool = "2core",
    deps = [
        ":http_protocol_integration_lib",
        ":socket_interface_swap_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "//source/extensions/filters/http/buffer:config",
        "//test/common/http/http2:http2_frame",
        "//test/integration/filters:add_invalid_data_filter_lib",
        "//test/integration/filters:buffer_continue_filter_lib",
        "//test/integration/filters:continue_after_local_reply_filter_lib",
        "//test/integration/filters:continue_headers_only_inject_body",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/integration/filters:invalid_header_filter_lib",
        "//test/integration/filters:local_reply_during_decoding_filter_lib",
        "//test/integration/filters:local_reply_during_encoding_data_filter_lib",
        "//test/integration/filters:local_reply_during_encoding_filter_lib",
        "//test/integration/filters:local_reply_with_metadata_filter_lib",
        "//test/integration/filters:metadata_control_filter_lib",
        "//test/integration/filters:random_pause_filter_lib",
        "//test/integration/filters:remove_response_headers_lib",
        "//test/integration/filters:remove_response_trailers_lib",
        "//test/integration/filters:stop_iteration_headers_inject_body",
        "//test/test_common:logging_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "protocol_integration_test",
    size = "enormous",
    srcs = [
        "instantiate_protocol_integration_test.cc",
    ],
    rbe_pool = "2core",
    # As this test has many H1/H2/v4/v6 tests it takes a while to run.
    # Shard it enough to bring the run time in line with other integration tests.
    shard_count = 16,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":protocol_integration_test_lib",
    ],
)

envoy_cc_test(
    name = "multi_envoy_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "multi_envoy_test.cc",
    ]),
    rbe_pool = "2core",
    deps = [
        ":http_protocol_integration_lib",
    ],
)

envoy_cc_test(
    name = "multiplexed_upstream_integration_test",
    size = "large",
    srcs = [
        "multiplexed_upstream_integration_test.cc",
    ],
    rbe_pool = "2core",
    shard_count = 4,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/buffer:config",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/integration/filters:random_pause_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "integration_admin_test_lib",
    hdrs = ["integration_admin_test.h"],
    deps = [
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_admin_test",
    size = "large",
    srcs = envoy_select_admin_functionality(
        ["integration_admin_test.cc"],
    ),
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//envoy/http:header_map_interface",
        "//source/common/stats:histogram_lib",
        "//source/common/stats:stats_matcher_lib",
        "//source/extensions/filters/http/buffer:config",
        "//test/common/stats:stat_test_utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ] + envoy_select_admin_functionality(
        ["integration_admin_test_lib"],
    ),
)

envoy_proto_library(
    name = "test_host_predicate_proto",
    srcs = ["test_host_predicate.proto"],
)

envoy_cc_test_library(
    name = "test_host_predicate_lib",
    hdrs = [
        "test_host_predicate.h",
        "test_host_predicate_config.h",
    ],
    deps = [
        ":test_host_predicate_proto_cc_proto",
        "//envoy/upstream:retry_interface",
    ],
)

# The light verson of this library allows Envoy mobile e2e tests to pull minimal
# dependencies, so keep as few libraries as possible in scope of the excepton-free builds.
envoy_cc_test_library(
    name = "http_integration_lib_light",
    srcs = [
        "http_integration.cc",
    ],
    hdrs = [
        "http_integration.h",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        ":integration_lib_light",
        ":test_host_predicate_lib",
        "//envoy/event:timer_interface",
        "//source/common/common:thread_annotations",
        "//source/common/network:socket_option_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//test/common/http/http2:http2_frame",
        "//test/common/upstream:utility_lib",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "http_integration_lib",
    deps = [
        ":common_extensions_lib",
        ":http_integration_lib_light",
        ":integration_lib",
        ":test_host_predicate_lib",
        "//test/integration/filters:add_body_filter_config_lib",
        "//test/integration/filters:add_trailers_filter_config_lib",
        "//test/integration/filters:call_decodedata_once_filter_config_lib",
        "//test/integration/filters:crash_filter_config_lib",
        "//test/integration/filters:decode_headers_return_stop_all_filter_config_lib",
        "//test/integration/filters:encode_headers_return_stop_all_filter_config_lib",
        "//test/integration/filters:modify_buffer_filter_config_lib",
        "//test/integration/filters:passthrough_filter_config_lib",
        "//test/integration/filters:pause_filter_lib",
        "//test/integration/filters:wait_for_whole_request_and_response_config_lib",
    ],
)

envoy_cc_test_library(
    name = "http_protocol_integration_lib",
    srcs = [
        "http_protocol_integration.cc",
    ],
    hdrs = [
        "http_protocol_integration.h",
    ],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//test/common/upstream:utility_lib",
        "//test/integration/filters:stream_info_to_headers_filter_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "idle_timeout_integration_test",
    size = "large",
    srcs = ["idle_timeout_integration_test.cc"],
    rbe_pool = "2core",
    # As this test has many pauses for idle timeouts, it takes a while to run.
    # Shard it enough to bring the run time in line with other integration tests.
    shard_count = 8,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//test/integration/filters:backpressure_filter_config_lib",
        "//test/integration/filters:reset_idle_timer_filter_lib",
        "//test/integration/filters:set_route_filter_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "integration_stream_decoder_lib",
    srcs = [
        "integration_stream_decoder.cc",
    ],
    hdrs = [
        "integration_stream_decoder.h",
    ],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:metadata_interface",
        "//source/common/common:dump_state_utils",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "integration_tcp_client_lib",
    srcs = [
        "integration_tcp_client.cc",
    ],
    hdrs = [
        "integration_tcp_client.h",
    ],
    deps = [
        ":utility_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:address_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:socket_interface",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "fake_upstream_lib",
    srcs = [
        "fake_upstream.cc",
    ],
    hdrs = [
        "fake_upstream.h",
    ],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//envoy/api:api_interface",
        "//envoy/grpc:status",
        "//envoy/http:codec_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/stats:stats_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:basic_resource_lib",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:linked_object",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/http/http3:codec_stats_lib",
        "//source/common/listener_manager:active_raw_udp_listener_config",
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks/http:header_validator_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:test_time_system_interface",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/quic:active_quic_listener_lib",
        "//source/common/quic:quic_client_factory_lib",
        "//source/common/quic:quic_server_factory_lib",
        "@com_github_google_quiche//:quic_test_tools_session_peer_lib",
    ]) + envoy_select_envoy_mobile_listener([
        "//source/common/listener_manager:listener_manager_lib",
    ]),
)

envoy_cc_test_library(
    name = "common_extensions_lib",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:rds_lib",
        "//source/common/router:scoped_rds_lib",
        "//source/extensions/clusters/eds:eds_lib",
        "//source/extensions/clusters/static:static_cluster_lib",
        "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
        "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/load_balancing_policies/least_request:config",
        "//source/extensions/load_balancing_policies/maglev:config",
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//source/extensions/network/dns_resolver/cares:config",
    ],
)

envoy_cc_test_library(
    name = "base_integration_test_lib",
    srcs = [
        "base_integration_test.cc",
    ],
    hdrs = [
        "base_integration_test.h",
    ],
    rbe_pool = "2core",
    deps = [
        ":autonomous_upstream_lib",
        ":fake_upstream_lib",
        ":integration_tcp_client_lib",
        ":utility_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:api_version_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:ssl_socket_lib",
        "//source/extensions/request_id/uuid:config",
        "//source/server:process_context_lib",
        "//source/server:proto_descriptors_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ] + select({
        "//bazel:apple": ["//source/extensions/network/dns_resolver/apple:config"],
        "//conditions:default": [],
    }) + envoy_select_enable_yaml([
        "//source/extensions/config_subscription/filesystem:filesystem_subscription_lib",
    ]),
)

envoy_cc_test_library(
    name = "autonomous_upstream_lib",
    srcs = [
        "autonomous_upstream.cc",
    ],
    hdrs = [
        "autonomous_upstream.h",
    ],
    rbe_pool = "2core",
    deps = [
        ":fake_upstream_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    srcs = [
        "server.cc",
        "ssl_utility.cc",
        "utility.cc",
    ],
    hdrs = [
        "server.h",
        "ssl_utility.h",
        "utility.h",
    ],
    data = ["//test/common/runtime:filesystem_test_data"],
    rbe_pool = "2core",
    deps = [
        ":server_stats_interface",
        ":tcp_dump",
        "//envoy/api:api_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:header_validator_factory_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:options_interface",
        "//envoy/server:process_context_interface",
        "//envoy/stats:stats_interface",
        "//source/common/api:api_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/common/stats:allocator_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_config_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//source/server:drain_manager_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server:listener_hooks_lib",
        "//source/server:options_base",
        "//source/server:process_context_lib",
        "//source/server:server_lib",
        "//test/common/upstream:utility_lib",
        "//test/config:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:test_time_system_interface",
        "@com_google_absl//absl/synchronization",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

# The light verson of this library allows Envoy mobile e2e tests to pull minimal
# dependencies, so keep as few libraries as possible in scope of the excepton-free builds.
envoy_cc_test_library(
    name = "integration_lib_light",
    hdrs = [
        "integration.h",
    ],
    deps = [
        ":autonomous_upstream_lib",
        ":base_integration_test_lib",
        ":fake_upstream_lib",
        ":integration_stream_decoder_lib",
        ":integration_tcp_client_lib",
        ":utility_lib",
        "//envoy/api:api_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:status",
        "//envoy/http:codec_interface",
        "//envoy/http:header_map_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:configuration_interface",
        "//envoy/server:hot_restart_interface",
        "//envoy/server:options_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread:thread_interface",
        "//source/common/api:api_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:basic_resource_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/common/tls:server_context_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:drain_manager_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server:listener_hooks_lib",
        "//source/server:process_context_lib",
        "//source/server:server_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/common/upstream:utility_lib",
        "//test/config:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:retry_priority_factory_mocks",
        "//test/mocks/upstream:retry_priority_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:test_time_system_interface",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "integration_lib",
    hdrs = [
        "integration.h",
    ],
    deps = [
        ":common_extensions_lib",
        ":integration_lib_light",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/health_checkers/grpc:health_checker_lib",
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
    ],
)

envoy_cc_test_library(
    name = "integration_test_lib",
    hdrs = ["integration_test.h"],
)

envoy_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_test_lib",
        "//envoy/registry",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/filters/http/match_delegate:config",
        "//test/integration/filters:buffer_continue_filter_lib",
        "//test/integration/filters:clear_route_cache_filter_lib",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/integration/filters:invalid_header_filter_lib",
        "//test/integration/filters:process_context_lib",
        "//test/integration/filters:set_response_code_filter_config_proto_cc_proto",
        "//test/integration/filters:set_response_code_filter_lib",
        "//test/integration/filters:set_route_filter_lib",
        "//test/integration/filters:stop_iteration_and_continue",
        "//test/mocks/http:http_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "redirect_integration_test",
    size = "large",
    srcs = [
        "redirect_integration_test.cc",
    ],
    rbe_pool = "2core",
    shard_count = 2,
    tags = [
        "cpu:3",
        "nofips",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//source/common/http:header_map_lib",
        "//test/integration/filters:pause_filter_for_quic_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "regex_engine_integration_test",
    size = "large",
    srcs = ["regex_engine_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":integration_lib",
        "//source/common/common:regex_lib",
        "@envoy_api//envoy/extensions/regex_engines/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "websocket_integration_test_lib",
    hdrs = ["websocket_integration_test.h"],
)

envoy_cc_test(
    name = "websocket_integration_test",
    size = "large",
    srcs = ["websocket_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        ":websocket_integration_test_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/http/buffer:config",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "socket_interface_integration_test",
    size = "large",
    srcs = ["socket_interface_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/common/network:socket_interface_lib",
        "//source/extensions/filters/network/echo:config",
    ],
)

envoy_cc_test(
    name = "stats_integration_test",
    size = "large",
    srcs = ["stats_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    # The symbol table cluster memory tests take a while to run specially under tsan.
    deps = [
        ":integration_lib",
        "//source/common/memory:stats_lib",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//test/common/memory:memory_test_utility_lib",
        "//test/config/integration/certs:certs_info",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "load_stats_integration_test",
    size = "large",
    srcs = ["load_stats_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//test/config:utility_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/load_stats/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "hds_integration_test",
    size = "large",
    srcs = ["hds_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        "//envoy/upstream:upstream_interface",
        "//source/common/config:metadata_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:health_checker_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//test/common/upstream:utility_lib",
        "//test/config:utility_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/health/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "header_prefix_integration_test",
    size = "large",
    srcs = ["header_prefix_integration_test.cc"],
    coverage = False,
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "fake_resource_monitor_lib",
    srcs = [
        "fake_resource_monitor.cc",
    ],
    hdrs = [
        "fake_resource_monitor.h",
    ],
    deps = [
        "//envoy/server:resource_monitor_config_interface",
        "//test/common/config:dummy_config_proto_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "base_overload_integration_test_lib",
    srcs = [
        "base_overload_integration_test.cc",
    ],
    hdrs = [
        "base_overload_integration_test.h",
    ],
    deps = [
        ":fake_resource_monitor_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "overload_integration_test",
    size = "large",
    srcs = ["overload_integration_test.cc"],
    rbe_pool = "2core",
    shard_count = 4,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":base_overload_integration_test_lib",
        ":http_protocol_integration_lib",
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//test/extensions/quic/proof_source:pending_proof_source_factory_impl_lib",
    ]),
)

envoy_cc_test(
    name = "rtds_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "rtds_integration_test.cc",
    ]),
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "extension_discovery_integration_test",
    size = "large",
    srcs = ["extension_discovery_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/extensions/filters/http/match_delegate:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/integration/filters:set_is_terminal_filter_config_proto_cc_proto",
        "//test/integration/filters:set_is_terminal_filter_lib",
        "//test/integration/filters:set_response_code_filter_config_proto_cc_proto",
        "//test/integration/filters:set_response_code_filter_lib",
        "//test/integration/filters:test_listener_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/common/matching/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/extension/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "listener_extension_discovery_integration_test",
    size = "large",
    srcs = ["listener_extension_discovery_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration/filters:test_listener_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/extension/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "network_extension_discovery_integration_test",
    size = "large",
    srcs = ["network_extension_discovery_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration/filters:test_network_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/extension/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "server_stats_interface",
    hdrs = ["server_stats.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/stats:stats_interface",
    ],
)

envoy_cc_test(
    name = "sds_static_integration_test",
    size = "large",
    srcs = [
        "sds_static_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sds_dynamic_integration_test",
    size = "large",
    srcs = [
        "sds_dynamic_integration_test.cc",
    ],
    data = [
        "sds_dynamic_key_rotation_setup.sh",
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    # TODO(envoyproxy/windows-dev): The key rotation in SdsDynamicKeyRotationIntegrationTest via
    # TestEnvironment::renameFile() fails on Windows. The renameFile() implementation does not
    # correctly handle symlinks.
    tags = [
        "cpu:3",
        "fails_on_clang_cl",
        "fails_on_windows",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/common/tls:test_private_key_method_provider_test_lib",
        "//test/config/integration/certs:certs_info",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sds_generic_secret_integration_test",
    size = "large",
    srcs = [
        "sds_generic_secret_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//envoy/registry",
        "//source/common/grpc:common_lib",
        "//test/config:v2_link_hacks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "tcp_proxy_integration_proto",
    srcs = [":tcp_proxy_integration_test.proto"],
)

envoy_cc_test_library(
    name = "tcp_proxy_integration_test_lib",
    hdrs = ["tcp_proxy_integration_test.h"],
)

envoy_cc_test(
    name = "tcp_proxy_integration_test",
    size = "large",
    srcs = ["tcp_proxy_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":fake_access_log_lib",
        ":integration_lib",
        ":tcp_proxy_integration_proto_cc_proto",
        ":tcp_proxy_integration_test_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/tcp_proxy/v2:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tcp_proxy_many_connections_test",
    size = "large",
    srcs = [
        "tcp_proxy_many_connections_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "tcp_tunneling_integration_test",
    size = "large",
    srcs = [
        "tcp_tunneling_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    shard_count = 30,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":http_protocol_integration_lib",
        ":tcp_tunneling_integration_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/upstreams/http/tcp:config",
        "//test/integration/filters:add_header_filter_config_lib",
        "//test/integration/filters:add_header_filter_proto_cc_proto",
        "//test/integration/filters:stop_iteration_and_continue",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/tcp/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "tcp_tunneling_integration_lib",
    srcs = [
        "tcp_tunneling_integration.cc",
    ],
    hdrs = [
        "tcp_tunneling_integration.h",
    ],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
    ],
)

envoy_cc_test(
    name = "udp_tunneling_integration_test",
    size = "large",
    srcs = [
        "udp_tunneling_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        ":http_protocol_integration_lib",
        "//source/common/stream_info:utility_lib",
        "//source/extensions/filters/udp/udp_proxy:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/http_capsule:config",
        "//source/extensions/upstreams/http/udp:config",
        "//test/extensions/filters/udp/udp_proxy/session_filters:drainer_filter_config_lib",
        "//test/extensions/filters/udp/udp_proxy/session_filters:drainer_filter_proto_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/udp/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tcp_conn_pool_integration_test",
    size = "large",
    srcs = [
        "tcp_conn_pool_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":integration_lib",
        "//envoy/server:filter_config_interface",
        "//envoy/tcp:conn_pool_interface",
        "//test/server:utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "tcp_async_client_integration_test",
    size = "large",
    srcs = [
        "tcp_async_client_integration_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":integration_lib",
        "//test/integration/filters:test_network_async_tcp_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "tcp_dump",
    srcs = ["tcp_dump.cc"],
    hdrs = ["tcp_dump.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:fmt_lib",
    ],
)

envoy_cc_test_library(
    name = "tracked_watermark_buffer_lib",
    srcs = [
        "tracked_watermark_buffer.cc",
    ],
    hdrs = [
        "tracked_watermark_buffer.h",
    ],
    deps = [
        "//source/common/buffer:watermark_buffer_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tracked_watermark_buffer_test",
    size = "large",
    srcs = ["tracked_watermark_buffer_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":tracked_watermark_buffer_lib",
        "//test/mocks/http:stream_reset_handler_mock",
    ],
)

envoy_cc_test_library(
    name = "uds_integration_test_lib",
    hdrs = ["uds_integration_test.h"],
    deps = ["@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto"],
)

envoy_cc_test(
    name = "uds_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "uds_integration_test.cc",
    ]),
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/stats:stats_lib",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ] + envoy_select_admin_functionality([
        "uds_integration_test_lib",
    ]),
)

envoy_cc_test(
    name = "dynamic_validation_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality(
        ["dynamic_validation_integration_test.cc"],
    ),
    data = ["//test/config/integration:server_xds_files"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/stats:stats_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "xds_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "xds_integration_test.cc",
    ]),
    data = [
        "//test/config/integration:server_xds_files",
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":http_protocol_integration_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/matching/network/application_protocol:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration/filters:test_listener_filter_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "xfcc_integration_test_lib",
    hdrs = ["xfcc_integration_test.h"],
    deps = ["@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto"],
)

envoy_cc_test(
    name = "xfcc_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "xfcc_integration_test.cc",
    ]),
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ] + envoy_select_admin_functionality([
        "xfcc_integration_test_lib",
    ]),
)

H1_FUZZ_LIB_DEPS = [
    ":capture_fuzz_proto_cc_proto",
    ":http_integration_lib",
    "//source/common/common:assert_lib",
    "//source/common/common:logger_lib",
    "//test/fuzz:fuzz_runner_lib",
    "//test/integration:integration_lib",
    "//test/test_common:environment_lib",
]

envoy_cc_test_library(
    name = "h1_fuzz_lib",
    srcs = ["h1_fuzz.cc"],
    hdrs = ["h1_fuzz.h"],
    rbe_pool = "2core",
    deps = H1_FUZZ_LIB_DEPS,
)

envoy_cc_test_library(
    name = "h1_fuzz_persistent_lib",
    srcs = ["h1_fuzz.cc"],
    hdrs = ["h1_fuzz.h"],
    copts = ["-DPERSISTENT_FUZZER"],
    rbe_pool = "2core",
    deps = H1_FUZZ_LIB_DEPS,
)

envoy_cc_fuzz_test(
    name = "h1_capture_fuzz_test",
    srcs = ["h1_capture_fuzz_test.cc"],
    corpus = "h1_corpus",
    rbe_pool = "2core",
    deps = [":h1_fuzz_lib"],
)

envoy_cc_fuzz_test(
    name = "h1_capture_persistent_fuzz_test",
    srcs = ["h1_capture_fuzz_test.cc"],
    copts = ["-DPERSISTENT_FUZZER"],
    corpus = "h1_corpus",
    rbe_pool = "2core",
    deps = [":h1_fuzz_persistent_lib"],
)

envoy_cc_fuzz_test(
    name = "h1_capture_direct_response_fuzz_test",
    srcs = ["h1_capture_direct_response_fuzz_test.cc"],
    corpus = "h1_corpus",
    rbe_pool = "2core",
    deps = [
        ":h1_fuzz_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "h1_capture_direct_response_persistent_fuzz_test",
    srcs = ["h1_capture_direct_response_fuzz_test.cc"],
    copts = ["-DPERSISTENT_FUZZER"],
    corpus = "h1_corpus",
    rbe_pool = "2core",
    deps = [
        ":h1_fuzz_persistent_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

H2_FUZZ_LIB_DEPS = [
    ":h2_capture_fuzz_proto_cc_proto",
    ":http_integration_lib",
    "//source/common/common:assert_lib",
    "//source/common/common:logger_lib",
    "//test/common/http/http2:http2_frame",
    "//test/fuzz:fuzz_runner_lib",
    "//test/fuzz:utility_lib",
    "//test/integration:integration_lib",
    "//test/test_common:environment_lib",
]

envoy_cc_test_library(
    name = "h2_fuzz_lib",
    srcs = ["h2_fuzz.cc"],
    hdrs = ["h2_fuzz.h"],
    rbe_pool = "2core",
    deps = H2_FUZZ_LIB_DEPS,
)

envoy_cc_test_library(
    name = "h2_fuzz_persistent_lib",
    srcs = ["h2_fuzz.cc"],
    hdrs = ["h2_fuzz.h"],
    copts = ["-DPERSISTENT_FUZZER"],
    rbe_pool = "2core",
    deps = H2_FUZZ_LIB_DEPS,
)

envoy_cc_fuzz_test(
    name = "h2_capture_fuzz_test",
    srcs = ["h2_capture_fuzz_test.cc"],
    corpus = "h2_corpus",
    rbe_pool = "2core",
    deps = [":h2_fuzz_lib"],
)

envoy_cc_fuzz_test(
    name = "h2_capture_persistent_fuzz_test",
    srcs = ["h2_capture_fuzz_test.cc"],
    copts = ["-DPERSISTENT_FUZZER"],
    corpus = "h2_corpus",
    rbe_pool = "2core",
    deps = [":h2_fuzz_persistent_lib"],
)

envoy_cc_fuzz_test(
    name = "h2_capture_direct_response_fuzz_test",
    srcs = ["h2_capture_direct_response_fuzz_test.cc"],
    corpus = "h2_corpus",
    rbe_pool = "2core",
    deps = [
        ":h2_fuzz_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "h2_capture_direct_response_persistent_fuzz_test",
    srcs = ["h2_capture_direct_response_fuzz_test.cc"],
    copts = ["-DPERSISTENT_FUZZER"],
    corpus = "h2_corpus",
    rbe_pool = "2core",
    deps = [
        ":h2_fuzz_persistent_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "scoped_rds_test_lib",
    hdrs = ["scoped_rds.h"],
    deps = [
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "scoped_rds_lib",
    size = "large",
    rbe_pool = "2core",
    deps = [
        ":http_integration_lib",
        ":scoped_rds_test_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "scoped_rds_integration_test",
    size = "large",
    srcs = [
        "scoped_rds_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":scoped_rds_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "listener_lds_integration_test",
    size = "large",
    srcs = [
        "listener_lds_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/integration/filters:address_restore_listener_filter_lib",
        "//test/integration/filters:inspect_data_listener_filter_lib",
        "//test/integration/filters:set_response_code_filter_config_proto_cc_proto",
        "//test/integration/filters:set_response_code_filter_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cx_limit_integration_test",
    size = "large",
    srcs = ["cx_limit_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/config:utility_lib",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "direct_response_integration_test",
    size = "large",
    srcs = ["direct_response_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:2",
    ],
    deps = [
        ":http_integration_lib",
    ],
)

envoy_cc_test(
    name = "local_reply_integration_test",
    size = "large",
    srcs = [
        "local_reply_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:2",
    ],
    deps = [
        ":http_integration_lib",
        ":http_protocol_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "command_formatter_extension_integration_test",
    size = "large",
    srcs = [
        "command_formatter_extension_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//test/common/formatter:command_extension_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "health_check_integration_test",
    size = "large",
    srcs = ["health_check_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        "//test/common/http/http2:http2_frame",
        "//test/config:v2_link_hacks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cluster_upstream_extension_integration_test",
    size = "large",
    srcs = [
        "cluster_upstream_extension_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/protobuf",
        "//test/integration/upstreams:per_host_upstream_config",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

# TODO(mattklein123): Use target_compatible_with when we switch to Bazel 4.0 instead of multiple
# selects.
envoy_cc_test(
    name = "quic_protocol_integration_test",
    size = "enormous",
    srcs = select({
        "//bazel:disable_http3": [],
        "//bazel:disable_admin_functionality": [],
        "//conditions:default": ["quic_protocol_integration_test.cc"],
    }),
    data = ["//test/config/integration/certs"],
    rbe_pool = "2core",
    shard_count = 16,
    tags = [
        "cpu:4",
        "nofips",
    ],
    deps = select({
        "//bazel:disable_http3": [],
        "//bazel:disable_admin_functionality": [],
        "//conditions:default": [
            ":protocol_integration_test_lib",
            "//source/common/quic:active_quic_listener_lib",
            "//source/common/quic:client_connection_factory_lib",
            "//source/common/quic:quic_client_factory_lib",
            "//source/common/quic:quic_server_factory_lib",
            "//source/common/quic:quic_transport_socket_factory_lib",
            "//test/integration/filters:pause_filter_for_quic_lib",
        ],
    }),
)

# TODO(mattklein123): Use target_compatible_with when we switch to Bazel 4.0 instead of multiple
# selects.
envoy_cc_test(
    name = "quic_http_integration_test",
    size = "large",
    srcs = select({
        "//bazel:disable_http3": [],
        "//bazel:disable_admin_functionality": [],
        "//conditions:default": ["quic_http_integration_test.cc"],
    }),
    data = ["//test/config/integration/certs"],
    rbe_pool = "2core",
    # TODO(envoyproxy/windows-dev): Diagnose failure shown only on clang-cl build, see:
    #   https://gist.github.com/wrowe/a152cb1d12c2f751916122aed39d8517
    # TODO(envoyproxy/windows-dev): Diagnose timeout, why opt build test under Windows GCP RBE
    # takes 10x as long as on linux (>300s vs ~30s). Shards = 2 solves for windows, see:
    #   https://github.com/envoyproxy/envoy/pull/13713/files#r512160087
    # Each of these tests exceeds 20s;
    # QuicHttpIntegrationTests/QuicHttpIntegrationTest.MultipleQuicConnections[With|No]BPF*
    tags = [
        "cpu:3",
        "fails_on_clang_cl",
        "fails_on_windows",
        "nofips",
    ],
    deps = envoy_select_enable_http3([
        "//source/extensions/quic/connection_debug_visitor:envoy_quic_connection_debug_visitor_basic",
        "//source/extensions/quic/server_preferred_address:fixed_server_preferred_address_config_factory_config",
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/common/tls/cert_validator:timed_cert_validator",
        ":http_integration_lib",
        ":socket_interface_swap_lib",
        "//source/common/quic:client_connection_factory_lib",
        "//source/common/quic:envoy_quic_client_connection_lib",
        "//source/common/quic:envoy_quic_client_session_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//source/common/quic:envoy_quic_proof_verifier_lib",
        "//source/common/quic:quic_client_factory_lib",
        "//source/common/quic:quic_server_factory_lib",
        "//test/common/quic:test_utils_lib",
        "//test/integration/filters:encoder_decoder_buffer_filter_lib",
        "//test/integration/filters:stream_info_to_headers_filter_lib",
        "//test/integration/filters:test_network_filter_lib",
        "//test/integration/filters:test_listener_filter_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/server_preferred_address/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/connection_debug_visitor/v3:pkg_cc_proto",
        "@com_github_google_quiche//:quic_test_tools_session_peer_lib",
    ]),
)

envoy_cc_test(
    name = "original_ip_detection_integration_test",
    size = "large",
    srcs = [
        "original_ip_detection_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//source/extensions/http/original_ip_detection/custom_header:config",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/custom_header/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "access_log_integration_test",
    size = "large",
    srcs = [
        "access_log_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "weighted_cluster_integration_test",
    size = "large",
    srcs = ["weighted_cluster_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        "//test/integration/filters:repick_cluster_filter_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "fake_access_log_proto",
    srcs = ["fake_access_log.proto"],
)

envoy_cc_test_library(
    name = "fake_access_log_lib",
    hdrs = [
        "fake_access_log.h",
    ],
    deps = [
        ":fake_access_log_proto_cc_proto",
    ],
)

envoy_proto_library(
    name = "typed_metadata_integration_proto",
    srcs = ["typed_metadata_integration_test.proto"],
)

envoy_cc_test(
    name = "typed_metadata_integration_test",
    size = "large",
    srcs = [
        "typed_metadata_integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        ":typed_metadata_integration_proto_cc_proto",
        "//source/common/protobuf",
        "//test/integration/filters:listener_typed_metadata_filter_lib",
        "//test/server:utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "tcp_proxy_odcds_integration_test",
    size = "large",
    srcs = ["tcp_proxy_odcds_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":ads_integration_lib",
        ":fake_upstream_lib",
        ":http_integration_lib",
        "//source/common/common:macros",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "xds_delegate_extension_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality([
        "xds_delegate_extension_integration_test.cc",
    ]),
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":xds_delegate_test_config_proto_cc_proto",
        "//envoy/config:xds_resources_delegate_interface",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/test_common:registry_lib",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "xds_delegate_test_config_proto",
    srcs = ["xds_delegate_test_config.proto"],
)

envoy_cc_test(
    name = "xds_config_tracker_integration_test",
    size = "large",
    srcs = ["xds_config_tracker_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_integration_lib",
        ":xds_config_tracker_test_proto_cc_proto",
        "//envoy/config:subscription_interface",
        "//envoy/config:xds_config_tracker_interface",
        "//envoy/protobuf:message_validator_interface",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/test_common:registry_lib",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "xds_config_tracker_test_proto",
    srcs = ["xds_config_tracker_test.proto"],
)

envoy_cc_test(
    name = "default_header_validator_integration_test",
    size = "large",
    srcs = [
        "default_header_validator_integration_test.cc",
    ],
    rbe_pool = "2core",
    shard_count = 8,
    tags = [
        "cpu:3",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//source/common/http:character_set_validation_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/http/header_validators/envoy_default:character_tables",
        "//test/test_common:logging_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
    ],
)

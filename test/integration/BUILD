licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_hot_restart",
    "envoy_sh_test",
)

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
    deps = [
        ":http_integration_lib",
        "//source/common/common:version_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:resources_lib",
        "//source/common/config:version_converter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/test_common:network_utility_lib",
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
    srcs = ["ads_integration_test.cc"],
    deps = [
        ":ads_integration_lib",
        ":http_integration_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:resources_lib",
        "//source/common/protobuf:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "api_listener_integration_test",
    srcs = ["api_listener_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "//test/mocks/http:stream_encoder_mock",
        "//test/server:utility_lib",
    ],
)

envoy_cc_test(
    name = "api_version_integration_test",
    srcs = ["api_version_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

py_binary(
    name = "capture_fuzz_gen",
    srcs = ["capture_fuzz_gen.py"],
    licenses = ["notice"],  # Apache 2
    python_version = "PY3",
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

envoy_cc_test(
    name = "cds_integration_test",
    srcs = ["cds_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:resources_lib",
        "//source/common/protobuf:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "eds_integration_test",
    srcs = ["eds_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "//source/common/upstream:load_balancer_lib",
        "//test/config:utility_lib",
        "//test/integration/filters:eds_ready_filter_config_lib",
        "//test/test_common:network_utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "filter_manager_integration_proto",
    srcs = [":filter_manager_integration_test.proto"],
)

envoy_cc_test(
    name = "filter_manager_integration_test",
    srcs = [
        "filter_manager_integration_test.cc",
        "filter_manager_integration_test.h",
    ],
    deps = [
        ":filter_manager_integration_proto_cc_proto",
        ":http_integration_lib",
        ":integration_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/server:utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cluster_filter_integration_test",
    srcs = ["cluster_filter_integration_test.cc"],
    deps = [
        ":integration_lib",
        "//include/envoy/network:filter_interface",
        "//include/envoy/registry",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/config:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "custom_cluster_integration_test",
    srcs = ["custom_cluster_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "//source/common/upstream:load_balancer_lib",
        "//test/config:utility_lib",
        "//test/integration/clusters:custom_static_cluster",
        "//test/test_common:network_utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "vhds_integration_test",
    srcs = ["vhds_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:resources_lib",
        "//source/common/protobuf:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

exports_files(["test_utility.sh"])

envoy_sh_test(
    name = "hotrestart_test",
    srcs = envoy_select_hot_restart([
        "hotrestart_test.sh",
    ]),
    data = [
        "test_utility.sh",
        "//source/exe:envoy-static",
        "//test/config/integration:server_config_files",
        "//tools:socket_passing",
    ],
)

envoy_sh_test(
    name = "run_envoy_test",
    srcs = ["run_envoy_test.sh"],
    data = [
        "test_utility.sh",
        "//source/exe:envoy-static",
        "//test/config/integration:server_config_files",
    ],
)

envoy_cc_test(
    name = "header_integration_test",
    srcs = [
        "header_integration_test.cc",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/protobuf",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http2_integration_test",
    srcs = [
        "http2_integration_test.cc",
        "http2_integration_test.h",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/filters/http/health_check:config",
        "//test/common/http/http2:http2_frame",
        "//test/integration/filters:metadata_stop_all_filter_config_lib",
        "//test/integration/filters:request_metadata_filter_config_lib",
        "//test/integration/filters:response_metadata_filter_config_lib",
        "//test/integration/filters:stop_iteration_and_continue",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http_subset_lb_integration_test",
    srcs = [
        "http_subset_lb_integration_test.cc",
    ],
    deps = [
        ":http_integration_lib",
        "//test/common/upstream:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "transport_socket_match_integration_test",
    srcs = [
        "transport_socket_match_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":http_integration_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/common/upstream:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "header_casing_integration_test",
    srcs = [
        "header_casing_integration_test.cc",
    ],
    deps = [
        ":http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http_timeout_integration_test",
    srcs = [
        "http_timeout_integration_test.cc",
        "http_timeout_integration_test.h",
    ],
    deps = [
        ":http_integration_lib",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "protocol_integration_test",
    srcs = [
        "protocol_integration_test.cc",
    ],
    # As this test has many H1/H2/v4/v6 tests it takes a while to run.
    # Shard it enough to bring the run time in line with other integration tests.
    shard_count = 5,
    deps = [
        ":http_protocol_integration_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/filters/http/health_check:config",
        "//test/integration/filters:random_pause_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http2_upstream_integration_test",
    srcs = [
        "http2_upstream_integration_test.cc",
        "http2_upstream_integration_test.h",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/access_loggers/grpc:http_config",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/filters/http/health_check:config",
        "//test/integration/filters:random_pause_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_admin_test",
    srcs = [
        "integration_admin_test.cc",
        "integration_admin_test.h",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//include/envoy/http:header_map_interface",
        "//source/common/stats:stats_matcher_lib",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/health_check:config",
        "//test/common/stats:stat_test_utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "test_host_predicate_lib",
    srcs = [
        "test_host_predicate.h",
        "test_host_predicate_config.h",
    ],
    deps = [
        "//include/envoy/upstream:retry_interface",
    ],
)

envoy_cc_test_library(
    name = "http_integration_lib",
    srcs = [
        "http_integration.cc",
    ],
    hdrs = [
        "http_integration.h",
    ],
    deps = [
        ":integration_lib",
        ":test_host_predicate_lib",
        "//include/envoy/event:timer_interface",
        "//source/common/common:thread_annotations",
        "//source/extensions/filters/http/on_demand:config",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//test/common/upstream:utility_lib",
        "//test/integration/filters:add_trailers_filter_config_lib",
        "//test/integration/filters:call_decodedata_once_filter_config_lib",
        "//test/integration/filters:decode_headers_return_stop_all_filter_config_lib",
        "//test/integration/filters:encode_headers_return_stop_all_filter_config_lib",
        "//test/integration/filters:headers_only_filter_config_lib",
        "//test/integration/filters:modify_buffer_filter_config_lib",
        "//test/integration/filters:passthrough_filter_config_lib",
        "//test/integration/filters:pause_filter_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
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
    deps = [
        ":http_integration_lib",
        "//test/common/upstream:utility_lib",
    ],
)

envoy_cc_test(
    name = "idle_timeout_integration_test",
    srcs = ["idle_timeout_integration_test.cc"],
    # As this test has many pauses for idle timeouts, it takes a while to run.
    # Shard it enough to bring the run time in line with other integration tests.
    shard_count = 2,
    deps = [
        ":http_protocol_integration_lib",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "integration_lib",
    srcs = [
        "autonomous_upstream.cc",
        "fake_upstream.cc",
        "integration.cc",
        "server.cc",
        "ssl_utility.cc",
        "utility.cc",
    ],
    hdrs = [
        "autonomous_upstream.h",
        "fake_upstream.h",
        "integration.h",
        "server.h",
        "ssl_utility.h",
        "utility.h",
    ],
    data = ["//test/common/runtime:filesystem_test_data"],
    deps = [
        ":server_stats_interface",
        ":tcp_dump",
        "//include/envoy/api:api_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/grpc:status",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:hot_restart_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread:thread_interface",
        "//source/common/api:api_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:version_converter_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tap:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/server:connection_handler_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server:listener_hooks_lib",
        "//source/server:process_context_lib",
        "//source/server:server_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/common/runtime:utility_lib",
        "//test/common/upstream:utility_lib",
        "//test/config:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:test_time_system_interface",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = [
        "integration_test.cc",
        "integration_test.h",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/cors:config",
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/filters/http/grpc_http1_bridge:config",
        "//source/extensions/filters/http/health_check:config",
        "//test/integration/filters:clear_route_cache_filter_lib",
        "//test/integration/filters:process_context_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "redirect_integration_test",
    srcs = [
        "redirect_integration_test.cc",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//source/common/http:header_map_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "websocket_integration_test",
    srcs = [
        "websocket_integration_test.cc",
        "websocket_integration_test.h",
    ],
    deps = [
        ":http_protocol_integration_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/http/buffer:config",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "echo_integration_test",
    srcs = [
        "echo_integration_test.cc",
    ],
    # This test must be run in exclusive mode: see comments in AddRemoveListener
    tags = ["exclusive"],
    deps = [
        ":integration_lib",
        "//source/extensions/filters/network/echo:config",
        "//test/server:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "stats_integration_test",
    srcs = ["stats_integration_test.cc"],
    # The symbol table cluster memory tests take a while to run specially under tsan.
    # Shard it to avoid test timeout.
    shard_count = 2,
    deps = [
        ":integration_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//test/common/stats:stat_test_utility_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "load_stats_integration_test",
    srcs = ["load_stats_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "//source/common/config:resources_lib",
        "//test/config:utility_lib",
        "//test/test_common:network_utility_lib",
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
    srcs = ["hds_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        ":integration_lib",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/config:metadata_lib",
        "//source/common/config:resources_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:health_checker_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//test/common/upstream:utility_lib",
        "//test/config:utility_lib",
        "//test/test_common:network_utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/health/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "header_prefix_integration_test",
    srcs = ["header_prefix_integration_test.cc"],
    coverage = False,
    deps = [
        ":http_protocol_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "overload_integration_test",
    srcs = ["overload_integration_test.cc"],
    deps = [
        ":http_protocol_integration_lib",
        "//source/extensions/resource_monitors/injected_resource:config",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "proxy_proto_integration_test",
    srcs = [
        "proxy_proto_integration_test.cc",
        "proxy_proto_integration_test.h",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codec_client_lib",
        "//source/extensions/filters/listener/proxy_protocol:config",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "ratelimit_integration_test",
    srcs = ["ratelimit_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/filters/http/ratelimit:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "rtds_integration_test",
    srcs = ["rtds_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "server_stats_interface",
    hdrs = ["server_stats.h"],
    deps = ["//include/envoy/stats:stats_interface"],
)

envoy_cc_test(
    name = "sds_static_integration_test",
    srcs = [
        "sds_static_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sds_dynamic_integration_test",
    srcs = [
        "sds_dynamic_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:resources_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sds_generic_secret_integration_test",
    srcs = [
        "sds_generic_secret_integration_test.cc",
    ],
    deps = [
        ":http_integration_lib",
        "//include/envoy/registry",
        "//source/common/grpc:common_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tcp_proxy_integration_test",
    srcs = [
        "tcp_proxy_integration_test.cc",
        "tcp_proxy_integration_test.h",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
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
    name = "tcp_tunneling_integration_test",
    srcs = [
        "tcp_tunneling_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":http_integration_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/tcp_proxy/v2:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tcp_conn_pool_integration_test",
    srcs = [
        "tcp_conn_pool_integration_test.cc",
    ],
    deps = [
        ":integration_lib",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/tcp:conn_pool_interface",
        "//test/server:utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
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

envoy_cc_test(
    name = "uds_integration_test",
    srcs = [
        "uds_integration_test.cc",
        "uds_integration_test.h",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/stats:stats_lib",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "version_integration_test",
    srcs = ["version_integration_test.cc"],
    deps = [
        ":http_integration_lib",
        "//source/extensions/filters/http/ip_tagging:config",
    ],
)

envoy_cc_test(
    name = "dynamic_validation_integration_test",
    srcs = ["dynamic_validation_integration_test.cc"],
    data = ["//test/config/integration:server_xds_files"],
    deps = [
        ":http_integration_lib",
        "//source/common/stats:stats_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "xds_integration_test",
    srcs = ["xds_integration_test.cc"],
    data = ["//test/config/integration:server_xds_files"],
    deps = [
        ":http_integration_lib",
        ":http_protocol_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "xfcc_integration_test",
    srcs = [
        "xfcc_integration_test.cc",
        "xfcc_integration_test.h",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
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
    deps = H1_FUZZ_LIB_DEPS,
)

envoy_cc_test_library(
    name = "h1_fuzz_persistent_lib",
    srcs = ["h1_fuzz.cc"],
    hdrs = ["h1_fuzz.h"],
    copts = ["-DPERSISTENT_FUZZER"],
    deps = H1_FUZZ_LIB_DEPS,
)

envoy_cc_fuzz_test(
    name = "h1_capture_fuzz_test",
    srcs = ["h1_capture_fuzz_test.cc"],
    corpus = "h1_corpus",
    deps = [":h1_fuzz_lib"],
)

envoy_cc_fuzz_test(
    name = "h1_capture_persistent_fuzz_test",
    srcs = ["h1_capture_fuzz_test.cc"],
    copts = ["-DPERSISTENT_FUZZER"],
    corpus = "h1_corpus",
    deps = [":h1_fuzz_persistent_lib"],
)

envoy_cc_fuzz_test(
    name = "h1_capture_direct_response_fuzz_test",
    srcs = ["h1_capture_direct_response_fuzz_test.cc"],
    corpus = "h1_corpus",
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
    deps = [
        ":h1_fuzz_persistent_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "scoped_rds_integration_test",
    srcs = [
        "scoped_rds_integration_test.cc",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:resources_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "listener_lds_integration_test",
    srcs = [
        "listener_lds_integration_test.cc",
    ],
    deps = [
        ":http_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:resources_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

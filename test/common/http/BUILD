load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "async_client_impl_test",
    srcs = ["async_client_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":common_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:async_client_lib",
        "//source/common/http:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/router:context_lib",
        "//source/extensions/upstreams/http/generic:config",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "async_client_utility_test",
    srcs = ["async_client_utility_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:async_client_utility_lib",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_cc_test(
    name = "character_set_validation_test",
    srcs = ["character_set_validation_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:character_set_validation_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "codec_client_test",
    srcs = ["codec_client_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":common_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:exception_lib",
        "//source/common/network:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:header_validator_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "codec_impl_fuzz_proto",
    srcs = ["codec_impl_fuzz.proto"],
    deps = ["//test/fuzz:common_proto"],
)

# TODO(kbaichoo): We split the fuzzer by protocol to allow the fuzzer to
# better explore the individual protocols and catagorize failures for the
# particular protocol that fails. In the future we might want to further
# split out the codec fuzzer to better model the individual protocols.
[
    envoy_cc_fuzz_test(
        name = "%s_codec_impl_fuzz_test" % http_protocol,
        srcs = ["codec_impl_fuzz_test.cc"],
        corpus = "codec_impl_corpus",
        defines = ["FUZZ_PROTOCOL_%s" % http_protocol],
        rbe_pool = "2core",
        deps = [
            ":codec_impl_fuzz_proto_cc_proto",
            "//source/common/http:conn_manager_lib",
            "//source/common/http:header_map_lib",
            "//source/common/http:rds_lib",
            "//source/common/http/http1:codec_lib",
            "//source/common/http/http2:codec_lib",
            "//test/common/http/http2:codec_impl_test_util",
            "//test/fuzz:utility_lib",
            "//test/mocks/http:http_mocks",
            "//test/mocks/network:network_mocks",
            "//test/mocks/server:overload_manager_mocks",
            "//test/test_common:test_runtime_lib",
            "@com_github_google_quiche//:quiche_common_platform_test",
        ],
    )
    for http_protocol in [
        "http1",
        "http2",
    ]
]

envoy_cc_test(
    name = "filter_manager_test",
    srcs = ["filter_manager_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:filter_manager_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_reply:local_reply_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "codec_wrappers_test",
    srcs = ["codec_wrappers_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:codec_wrappers_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "codes_test",
    srcs = ["codes_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:empty_string",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "codes_speed_test",
    srcs = ["codes_speed_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:codes_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "@com_github_google_benchmark//:benchmark",
    ],
)

envoy_benchmark_test(
    name = "codes_speed_test_benchmark_test",
    benchmark_binary = "codes_speed_test",
)

envoy_cc_test_library(
    name = "common_lib",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    deps = [
        "//envoy/http:conn_pool_interface",
        "//envoy/http:header_map_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/http:http3_status_tracker_impl_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_proto_library(
    name = "conn_manager_impl_fuzz_proto",
    srcs = ["conn_manager_impl_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "conn_manager_impl_fuzz_test",
    srcs = ["conn_manager_impl_fuzz_test.cc"],
    corpus = "conn_manager_impl_corpus",
    rbe_pool = "2core",
    deps = [
        ":conn_manager_impl_fuzz_proto_cc_proto",
        "//source/common/common:empty_string",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:context_lib",
        "//source/common/http:date_provider_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:rds_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/server:drain_manager_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "conn_manager_impl_test_base_lib",
    srcs = ["conn_manager_impl_test_base.cc"],
    hdrs = ["conn_manager_impl_test_base.h"],
    rbe_pool = "2core",
    deps = [
        ":xff_extension_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:context_lib",
        "//source/common/http:rds_lib",
        "//source/common/tracing:custom_tag_lib",
        "//source/extensions/access_loggers/common:file_access_log_lib",
        "//source/extensions/http/header_validators/envoy_default:http1_header_validator",
        "//source/extensions/request_id/uuid:config",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:early_header_mutation_mock",
        "//test/mocks/http:header_validator_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:delegating_route_utility_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "conn_manager_impl_test",
    srcs = [
        # Split to avoid compiler OOM, especially on ASAN.
        "conn_manager_impl_test.cc",
        "conn_manager_impl_test_2.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":conn_manager_impl_test_base_lib",
        ":custom_header_extension_lib",
    ],
)

envoy_cc_test(
    name = "conn_manager_utility_test",
    srcs = ["conn_manager_utility_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":custom_header_extension_lib",
        ":xff_extension_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:headers_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/request_id/uuid:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "date_provider_impl_test",
    srcs = ["date_provider_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:date_provider_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_cc_test(
    name = "header_map_impl_test",
    srcs = ["header_map_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_list_view_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "header_map_impl_speed_test",
    srcs = ["header_map_impl_speed_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "@com_github_google_benchmark//:benchmark",
    ],
)

envoy_benchmark_test(
    name = "header_map_impl_speed_test_benchmark_test",
    benchmark_binary = "header_map_impl_speed_test",
)

envoy_proto_library(
    name = "header_map_impl_fuzz_proto",
    srcs = ["header_map_impl_fuzz.proto"],
)

envoy_cc_fuzz_test(
    name = "header_map_impl_fuzz_test",
    srcs = ["header_map_impl_fuzz_test.cc"],
    corpus = "header_map_impl_corpus",
    rbe_pool = "2core",
    deps = [
        ":header_map_impl_fuzz_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//test/fuzz:utility_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "inline_cookie_test",
    srcs = ["inline_cookie_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)

envoy_cc_test(
    name = "header_utility_test",
    srcs = ["header_utility_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_utility_lib",
        "//test/mocks/http:header_validator_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "user_agent_test",
    srcs = ["user_agent_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:user_agent_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "utility_fuzz_proto",
    srcs = ["utility_fuzz.proto"],
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)

envoy_cc_fuzz_test(
    name = "utility_fuzz_test",
    srcs = ["utility_fuzz_test.cc"],
    corpus = "utility_corpus",
    rbe_pool = "2core",
    deps = [
        ":utility_fuzz_proto_cc_proto",
        "//source/common/http:utility_lib",
        "//test/fuzz:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:address_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "metadata_interface_test",
    srcs = ["metadata_interface_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/http:metadata_interface",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_test(
    name = "path_utility_test",
    srcs = ["path_utility_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:path_utility_lib",
    ],
)

envoy_cc_test(
    name = "status_test",
    srcs = ["status_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:status_lib",
    ],
)

envoy_cc_test(
    name = "mixed_conn_pool_test",
    srcs = ["mixed_conn_pool_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":common_lib",
        "//source/common/http:mixed_conn_pool",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:http_server_properties_cache_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "conn_pool_grid_test",
    srcs = envoy_select_enable_http3(["conn_pool_grid_test.cc"]),
    rbe_pool = "2core",
    deps = envoy_select_enable_http3([
        ":common_lib",
        "//source/common/http:conn_pool_grid",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:test_runtime_lib",
        "//source/common/quic:quic_client_factory_lib",
        "//source/common/quic:quic_transport_socket_factory_lib",
        "//source/common/quic:client_connection_factory_lib",
    ]),
)

envoy_cc_test(
    name = "http3_status_tracker_impl_test",
    srcs = ["http3_status_tracker_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":common_lib",
        "//source/common/http:http3_status_tracker_impl_lib",
        "//test/mocks:common_lib",
    ],
)

envoy_cc_test(
    name = "http_server_properties_cache_impl_test",
    srcs = ["http_server_properties_cache_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":common_lib",
        "//source/common/http:http_server_properties_cache",
        "//test/mocks:common_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test(
    name = "http_server_properties_cache_manager_test",
    srcs = ["http_server_properties_cache_manager_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":common_lib",
        "//source/common/http:http_server_properties_cache",
        "//source/common/singleton:manager_impl_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_proto_library(
    name = "path_utility_fuzz_proto",
    srcs = ["path_utility_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "path_utility_fuzz_test",
    srcs = ["path_utility_fuzz_test.cc"],
    corpus = "path_utility_corpus",
    rbe_pool = "2core",
    deps = [
        ":path_utility_fuzz_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//source/common/http:path_utility_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "custom_header_extension_lib",
    srcs = ["custom_header_extension.cc"],
    hdrs = ["custom_header_extension.h"],
    deps = [
        "//source/extensions/http/original_ip_detection/custom_header:custom_header_lib",
    ],
)

envoy_cc_test_library(
    name = "xff_extension_lib",
    srcs = ["xff_extension.cc"],
    hdrs = ["xff_extension.h"],
    deps = [
        "//source/extensions/http/original_ip_detection/xff:xff_lib",
    ],
)

envoy_cc_test(
    name = "dependency_manager_test",
    srcs = ["dependency_manager_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:dependency_manager",
    ],
)

envoy_cc_test(
    name = "header_mutation_test",
    srcs = ["header_mutation_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:header_mutation_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "filter_chain_helper_test",
    srcs = ["filter_chain_helper_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:filter_chain_helper_lib",
        "//test/mocks:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/router:router_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "hcm_router_fuzz_proto",
    srcs = ["hcm_router_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "hcm_router_fuzz_test",
    srcs = ["hcm_router_fuzz_test.cc"],
    corpus = "hcm_router_fuzz_corpus",
    rbe_pool = "2core",
    deps = [
        ":conn_manager_impl_test_base_lib",
        ":hcm_router_fuzz_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:config_lib",
        "//source/extensions/upstreams/http/generic:config",
        "//test/fuzz:utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/upstreams/http/generic/v3:pkg_cc_proto",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_test(
    name = "async_client_impl_test",
    srcs = ["async_client_impl_test.cc"],
    deps = [
        ":common_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:async_client_lib",
        "//source/common/http:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test(
    name = "codec_client_test",
    srcs = ["codec_client_test.cc"],
    deps = [
        ":common_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:exception_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "codec_impl_fuzz_proto",
    srcs = ["codec_impl_fuzz.proto"],
    deps = ["//test/fuzz:common_proto"],
)

envoy_cc_fuzz_test(
    name = "codec_impl_fuzz_test",
    srcs = ["codec_impl_fuzz_test.cc"],
    corpus = "codec_impl_corpus",
    deps = [
        ":codec_impl_fuzz_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//test/common/http/http2:codec_impl_test_util",
        "//test/fuzz:utility_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "codec_wrappers_test",
    srcs = ["codec_wrappers_test.cc"],
    deps = [
        "//source/common/http:codec_wrappers_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "codes_test",
    srcs = ["codes_test.cc"],
    deps = [
        "//source/common/common:empty_string",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_binary(
    name = "codes_speed_test",
    srcs = ["codes_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/http:codes_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
    ],
)

envoy_cc_test_library(
    name = "common_lib",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:header_map_interface",
        "//source/common/http:codec_client_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_proto_library(
    name = "conn_manager_impl_fuzz_proto",
    srcs = ["conn_manager_impl_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "conn_manager_impl_fuzz_test",
    srcs = ["conn_manager_impl_fuzz_test.cc"],
    corpus = "conn_manager_impl_corpus",
    deps = [
        ":conn_manager_impl_fuzz_proto_cc_proto",
        "//source/common/common:empty_string",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:context_lib",
        "//source/common/http:date_provider_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "conn_manager_impl_test",
    srcs = ["conn_manager_impl_test.cc"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/access_log:access_log_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:context_lib",
        "//source/common/http:date_provider_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/network:address_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/access_loggers/file:file_access_log_lib",
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test(
    name = "conn_manager_utility_test",
    srcs = ["conn_manager_utility_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:headers_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/runtime:uuid_util_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "date_provider_impl_test",
    srcs = ["date_provider_impl_test.cc"],
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
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_binary(
    name = "header_map_impl_speed_test",
    srcs = ["header_map_impl_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/http:header_map_lib",
    ],
)

envoy_proto_library(
    name = "header_map_impl_fuzz_proto",
    srcs = ["header_map_impl_fuzz.proto"],
)

envoy_cc_fuzz_test(
    name = "header_map_impl_fuzz_test",
    srcs = ["header_map_impl_fuzz_test.cc"],
    corpus = "header_map_impl_corpus",
    deps = [
        ":header_map_impl_fuzz_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test(
    name = "header_utility_test",
    srcs = ["header_utility_test.cc"],
    deps = [
        "//source/common/http:header_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "user_agent_test",
    srcs = ["user_agent_test.cc"],
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
)

envoy_cc_fuzz_test(
    name = "utility_fuzz_test",
    srcs = ["utility_fuzz_test.cc"],
    corpus = "utility_corpus",
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
    deps = [
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:address_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "path_utility_test",
    srcs = ["path_utility_test.cc"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:path_utility_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_directory_genrule",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_impl_test",
    rbe_pool = "2core",
    deps = [":config_impl_test_lib"],
)

envoy_cc_test_library(
    name = "config_impl_test_lib",
    srcs = ["config_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":route_fuzz_proto_cc_proto",
        "//envoy/common:hashable_interface",
        "//source/common/config:metadata_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:config_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/upstream:retry_priority_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_impl_integration_test",
    size = "large",
    srcs = [
        "config_impl_integration_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/protobuf",
        "//source/common/router:config_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "config_impl_headermap_benchmark_test",
    srcs = ["config_impl_headermap_benchmark_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/router:config_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@com_github_google_benchmark//:benchmark",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "header_parser_fuzz_proto",
    srcs = ["header_parser_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/config/core/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "header_parser_fuzz_test",
    srcs = ["header_parser_fuzz_test.cc"],
    corpus = "header_parser_corpus",
    rbe_pool = "2core",
    deps = [
        ":header_parser_fuzz_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//source/common/router:header_parser_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test(
    name = "reset_header_parser_test",
    srcs = ["reset_header_parser_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_utility_lib",
        "//source/common/router:reset_header_parser_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "rds_impl_test",
    srcs = ["rds_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/http:rds_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/router:rds_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "scoped_config_impl_test",
    srcs = ["scoped_config_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/router:scoped_config_lib",
        "//test/mocks/router:router_mocks",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "scoped_rds_test",
    srcs = ["scoped_rds_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/config:subscription_interface",
        "//envoy/init:manager_interface",
        "//source/common/config:api_version_lib",
        "//source/common/config:null_grpc_mux_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:message_lib",
        "//source/common/http:rds_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/router:scoped_rds_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "vhds_test",
    srcs = ["vhds_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/router:rds_lib",
        "//source/common/router:vhds_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "retry_state_impl_test",
    srcs = ["retry_state_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/router:reset_header_parser_lib",
        "//source/common/router:retry_state_lib",
        "//source/common/upstream:resource_manager_lib",
        "//test/common/http:common_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "route_fuzz_proto",
    srcs = ["route_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/config/route/v3:pkg",
    ],
)

# envoy_cc_test_binary is generating mostly static binary regardless of config
envoy_cc_test_binary(
    name = "config_impl_test_static",
    rbe_pool = "2core",
    deps = [
        ":config_impl_test_lib",
        "//test:main",
    ],
)

sh_binary(
    name = "corpus_from_config_impl_sh",
    srcs = ["corpus_from_config_impl.sh"],
)

envoy_directory_genrule(
    name = "corpus_from_config_impl",
    testonly = 1,
    srcs = [
        # This is deliberately in srcs, since we run into host/target confusion
        # otherwise in oss-fuzz builds.
        ":config_impl_test_static",
    ],
    cmd = " ".join([
        "$(location corpus_from_config_impl_sh)",
        "$(location //test/common/router:config_impl_test_static)",
    ]),
    exec_properties = select({
        "//bazel:engflow_rbe": {"Pool": "2core"},
        "//conditions:default": {},
    }),
    tools = [":corpus_from_config_impl_sh"],
)

filegroup(
    name = "route_corpus",
    testonly = 1,
    srcs = select({
        # TODO(asraa): Clean this up for cross-compilation. Right now we assume
        # the host and target are the same on x86 builds, so we only execute the
        # corpus generation binary on x86 platforms.
        "//bazel:x86": [":corpus_from_config_impl"],
        "//conditions:default": [],
    }) + glob([
        "route_corpus/**",
    ]),
)

envoy_cc_fuzz_test(
    name = "route_fuzz_test",
    size = "large",
    srcs = ["route_fuzz_test.cc"],
    corpus = ":route_corpus",
    rbe_pool = "2core",
    # The :config_impl_test_static target does not build with coverage
    tags = ["nocoverage"],
    deps = [
        ":route_fuzz_proto_cc_proto",
        "//source/common/router:config_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "router_ratelimit_test",
    srcs = ["router_ratelimit_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/router:router_ratelimit_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "router_test",
    srcs = [
        # Split to avoid compiler OOM, especially on ASAN.
        "router_test.cc",
        "router_2_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":router_test_base_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:context_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:router_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/upstreams/http/generic:config",
        "//source/extensions/upstreams/http/http:config",
        "//source/extensions/upstreams/http/tcp:config",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/generic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/http/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/tcp/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/tcp/generic/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "router_test_base_lib",
    srcs = ["router_test_base.cc"],
    hdrs = ["router_test_base.h"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:context_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:router_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/upstreams/http/generic:config",
        "//source/extensions/upstreams/http/http:config",
        "//source/extensions/upstreams/http/tcp:config",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "router_upstream_log_test",
    srcs = ["router_upstream_log_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/upstreams/http/generic:config",
        "//test/common/http:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "router_upstream_filter_test",
    srcs = ["router_upstream_filter_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:router_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/upstreams/http/generic:config",
        "//test/common/http:common_lib",
        "//test/integration/filters:add_header_filter_config_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "shadow_writer_impl_test",
    srcs = ["shadow_writer_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/router:shadow_writer_lib",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test(
    name = "header_formatter_test",
    srcs = ["header_formatter_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/config:metadata_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/network:address_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//test/common/stream_info:test_int_accessor_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "string_accessor_impl_test",
    srcs = ["string_accessor_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/router:string_accessor_lib",
    ],
)

envoy_cc_test(
    name = "upstream_request_test",
    srcs = ["upstream_request_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/router:router_lib",
        "//test/common/http:common_lib",
        "//test/common/memory:memory_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/router:router_filter_interface",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "delegating_route_impl_test",
    srcs = ["delegating_route_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/router:config_lib",
        "//source/common/router:delegating_route_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_proto_library(
    name = "router_fuzz_proto",
    srcs = ["router_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg",
    ],
)

envoy_cc_benchmark_binary(
    name = "config_impl_speed_test",
    srcs = ["config_impl_speed_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/router:config_lib",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@com_github_google_benchmark//:benchmark",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "config_impl_benchmark_test",
    benchmark_binary = "config_impl_speed_test",
)

envoy_cc_benchmark_binary(
    name = "header_formatter_speed_test",
    srcs = ["header_formatter_speed_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/router:router_lib",
        "//test/common/stream_info:test_util",
        "@com_github_google_benchmark//:benchmark",
    ],
)

envoy_benchmark_test(
    name = "header_formatter_benchmark_test",
    benchmark_binary = "header_formatter_speed_test",
)

load(
    "//bazel:envoy_build_system.bzl",
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
    tags = ["fails_on_windows"],
    deps = [":config_impl_test_lib"],
)

envoy_cc_test_library(
    name = "config_impl_test_lib",
    srcs = ["config_impl_test.cc"],
    deps = [
        ":route_fuzz_proto_cc_proto",
        "//source/common/config:metadata_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:config_lib",
        "//source/common/stream_info:filter_state_lib",
        "//test/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
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
    deps = [
        ":header_parser_fuzz_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//source/common/router:header_parser_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test(
    name = "rds_impl_test",
    srcs = ["rds_impl_test.cc"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/router:rds_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
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
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//source/common/router:scoped_config_lib",
        "//test/mocks/router:router_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "scoped_rds_test",
    srcs = ["scoped_rds_test.cc"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/init:manager_interface",
        "//source/common/config:api_version_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:message_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/router:scoped_rds_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "vhds_test",
    srcs = ["vhds_test.cc"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/router:rds_lib",
        "//source/common/router:vhds_lib",
        "//source/server/admin:admin_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "retry_state_impl_test",
    srcs = ["retry_state_impl_test.cc"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/router:retry_state_lib",
        "//source/common/upstream:resource_manager_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
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
    srcs = ["route_fuzz_test.cc"],
    corpus = ":route_corpus",
    # The corpus_from_config_impl currently does not work, the tests it runs do not pass
    tags = ["skip_on_windows"],
    deps = [
        ":route_fuzz_proto_cc_proto",
        "//source/common/router:config_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "router_ratelimit_test",
    srcs = ["router_ratelimit_test.cc"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/router:router_ratelimit_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "router_test",
    srcs = ["router_test.cc"],
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
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/http/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/tcp/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "router_upstream_log_test",
    srcs = ["router_upstream_log_test.cc"],
    external_deps = ["abseil_optional"],
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/buffer:buffer_lib",
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
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "shadow_writer_impl_test",
    srcs = ["shadow_writer_impl_test.cc"],
    deps = [
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/router:shadow_writer_lib",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "header_formatter_test",
    srcs = ["header_formatter_test.cc"],
    deps = [
        "//source/common/config:metadata_lib",
        "//source/common/router:header_formatter_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//test/common/stream_info:test_int_accessor_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "string_accessor_impl_test",
    srcs = ["string_accessor_impl_test.cc"],
    deps = [
        "//source/common/router:string_accessor_lib",
    ],
)

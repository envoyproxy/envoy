licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_sh_test",
)

envoy_package()

envoy_sh_test(
    name = "hotrestart_test",
    srcs = ["hotrestart_test.sh"],
    data = [
        "//source/exe:envoy-static",
        "//test/common/runtime:filesystem_setup.sh",
        "//test/common/runtime:filesystem_test_data",
        "//test/config/integration:server_config_files",
        "//tools:socket_passing",
    ],
    # Need to disable test execution sandboxing for this test, since it uses shmem.
    # TODO(htuch): When most folks are using a Bazel with the recent
    # https://github.com/bazelbuild/bazel/commit/8d739f2fad3fd0135b940170691b0119c5baca1e,
    # we should be able to remove this workaround. See the various issues closed in the commit
    # message for further details.
    local = 1,
)

envoy_cc_test(
    name = "http2_integration_test",
    srcs = [
        "http2_integration_test.cc",
        "http2_integration_test.h",
    ],
    data = [
        "//test/config/integration:server_config_files",
        "//test/config/integration:server_http2.json",
    ],
    deps = [
        ":integration_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "http2_upstream_integration_test",
    srcs = [
        "http2_upstream_integration_test.cc",
        "http2_upstream_integration_test.h",
    ],
    data = ["//test/config/integration:server_http2_upstream.json"],
    deps = [
        ":integration_lib",
        "//source/common/http:header_map_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_admin_test",
    srcs = [
        "integration_admin_test.cc",
        "integration_admin_test.h",
    ],
    data = [
        "//test/config/integration:server.json",
        "//test/config/integration:server_config_files",
    ],
    deps = [
        ":integration_lib",
        "//include/envoy/http:header_map_interface",
    ],
)

envoy_cc_test_library(
    name = "integration_lib",
    srcs = [
        "fake_upstream.cc",
        "integration.cc",
        "server.cc",
        "utility.cc",
    ],
    hdrs = [
        "fake_upstream.h",
        "integration.h",
        "server.h",
        "utility.h",
    ],
    data = ["//test/common/runtime:filesystem_test_data"],
    deps = [
        "//include/envoy/api:api_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:hot_restart_interface",
        "//include/envoy/server:options_interface",
        "//source/common/api:api_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/server:connection_handler_lib",
        "//source/server:server_lib",
        "//source/server:test_hooks_lib",
        "//source/server/config/http:buffer_lib",
        "//source/server/config/http:dynamo_lib",
        "//source/server/config/http:fault_lib",
        "//source/server/config/http:grpc_http1_bridge_lib",
        "//source/server/config/http:lightstep_lib",
        "//source/server/config/http:ratelimit_lib",
        "//source/server/config/http:router_lib",
        "//source/server/config/http:zipkin_lib",
        "//source/server/config/network:client_ssl_auth_lib",
        "//source/server/config/network:echo_lib",
        "//source/server/config/network:http_connection_manager_lib",
        "//source/server/config/network:mongo_proxy_lib",
        "//source/server/config/network:ratelimit_lib",
        "//source/server/config/network:redis_proxy_lib",
        "//source/server/config/network:tcp_proxy_lib",
        "//source/server/http:health_check_lib",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = [
        "integration_test.cc",
        "integration_test.h",
    ],
    data = [
        "//test/config/integration:server.json",
        "//test/config/integration:server_config_files",
    ],
    deps = [
        ":integration_lib",
        "//source/common/http:header_map_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "echo_integration_test",
    srcs = [
        "echo_integration_test.cc",
    ],
    data = [
        "//test/config/integration:echo_server.json",
    ],
    deps = [
        ":integration_lib",
    ],
)

envoy_cc_test(
    name = "proxy_proto_integration_test",
    srcs = [
        "proxy_proto_integration_test.cc",
        "proxy_proto_integration_test.h",
    ],
    data = ["//test/config/integration:server_proxy_proto.json"],
    deps = [
        ":integration_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/stats:stats_lib",
    ],
)

envoy_cc_test(
    name = "ssl_integration_test",
    srcs = [
        "ssl_integration_test.cc",
        "ssl_integration_test.h",
    ],
    data = [
        "//test/config/integration:server_ssl.json",
        "//test/config/integration/certs",
    ],
    deps = [
        ":integration_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/ssl:context_config_lib",
        "//source/common/ssl:context_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)

envoy_cc_test(
    name = "xfcc_integration_test",
    srcs = [
        "ssl_integration_test.h",
        "xfcc_integration_test.cc",
        "xfcc_integration_test.h",
    ],
    data = [
        "//test/config/integration:server_xfcc.json",
        "//test/config/integration/certs",
    ],
    deps = [
        ":integration_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/ssl:context_config_lib",
        "//source/common/ssl:context_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)

envoy_cc_test(
    name = "uds_integration_test",
    srcs = [
        "uds_integration_test.cc",
        "uds_integration_test.h",
    ],
    data = ["//test/config/integration:server_uds.json"],
    deps = [
        ":integration_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/stats:stats_lib",
        "//test/test_common:environment_lib",
    ],
)

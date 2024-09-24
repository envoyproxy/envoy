load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "tcp_proxy_test_base",
    hdrs = [
        "tcp_proxy_test_base.h",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/network:address_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:upstream_server_name_lib",
        "//source/common/network:upstream_socket_options_filter_state_lib",
        "//source/common/stats:stats_lib",
        "//source/common/tcp_proxy",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/upstreams/http/generic:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/generic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/tcp/generic/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":tcp_proxy_test_base",
        "//envoy/common:hashable_interface",
    ],
)

envoy_cc_test(
    name = "tcp_proxy_test",
    srcs = [
        "tcp_proxy_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":tcp_proxy_test_base",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/generic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/tcp/generic/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "upstream_test",
    srcs = ["upstream_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/tcp_proxy",
        "//test/common/memory:memory_test_utility_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/router:router_filter_interface",
        "//test/mocks/router:upstream_request",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/test_common:test_runtime_lib",
    ],
)

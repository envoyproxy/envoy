load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "tcp_proxy_test",
    srcs = ["tcp_proxy_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:upstream_server_name_lib",
        "//source/common/stats:stats_lib",
        "//source/common/tcp_proxy",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/access_loggers:well_known_names",
        "//source/extensions/access_loggers/file:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "upstream_test",
    srcs = ["upstream_test.cc"],
    deps = [
        ":tcp_upstream_mocks",
        "//source/common/tcp_proxy:tcp_proxy_upstreams",
        "//source/common/tcp_proxy:upstream_interface",
        "//test/mocks/http:http_mocks",
        "//test/mocks/tcp:tcp_mocks",
    ],
)

envoy_cc_mock(
    name = "tcp_upstream_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/tcp_proxy:tcp_proxy_upstreams",
        "//source/common/tcp_proxy:upstream_interface",
        "//test/mocks/http:http_mocks",
        "//test/mocks/tcp:tcp_mocks",
    ],
)

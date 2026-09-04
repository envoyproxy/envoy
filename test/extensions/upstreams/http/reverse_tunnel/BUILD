load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:upstream_socket_manager_lib",
        "//source/extensions/upstreams/http/reverse_tunnel:config",
        "//test/common/http/http2:http2_frame",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/reverse_tunnel_reporting_service:reporter_mocks",
        "//test/mocks/server:admin_stream_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/reverse_tunnel/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "bridge_config_test",
    srcs = ["bridge_config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    deps = [
        "//source/extensions/upstreams/http/dynamic_modules:bridge_config_lib",
        "//test/extensions/dynamic_modules:util",
    ],
)

envoy_cc_test(
    name = "http_tcp_bridge_test",
    srcs = ["http_tcp_bridge_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_end_stream",
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_no_op",
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_stop_and_buffer",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/upstreams/http/dynamic_modules:abi_impl",
        "//source/extensions/upstreams/http/dynamic_modules:http_tcp_bridge_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/tcp:tcp_mocks",
    ],
)

envoy_cc_test(
    name = "conn_pool_test",
    srcs = ["conn_pool_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/upstreams/http/dynamic_modules:conn_pool_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/router:router_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:http_tcp_bridge_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/upstreams/http/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)

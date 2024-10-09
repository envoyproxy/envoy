load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_mock(
    name = "mocks_lib",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    rbe_pool = "2core",
    deps = [
        "//contrib/rocketmq_proxy/filters/network/source:config",
        "//contrib/rocketmq_proxy/filters/network/source/router:router_lib",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        "//contrib/rocketmq_proxy/filters/network/source:config",
    ],
)

envoy_cc_test(
    name = "protocol_test",
    srcs = ["protocol_test.cc"],
    deps = [
        "//contrib/rocketmq_proxy/filters/network/source:config",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "router_test",
    srcs = ["router_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//contrib/rocketmq_proxy/filters/network/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "topic_route_test",
    srcs = ["topic_route_test.cc"],
    deps = [
        "//contrib/rocketmq_proxy/filters/network/source:config",
        "//source/common/protobuf:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "conn_manager_test",
    srcs = ["conn_manager_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "active_message_test",
    srcs = ["active_message_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//contrib/rocketmq_proxy/filters/network/source:config",
        "//source/common/network:address_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/rocketmq_proxy/filters/network/source:config",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/rocketmq_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "codec_test",
    srcs = ["codec_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:address_lib",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:registry_lib",
    ],
)

envoy_cc_test(
    name = "route_matcher_test",
    srcs = ["route_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/rocketmq_proxy/filters/network/source/router:route_matcher",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/rocketmq_proxy/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "fake_codec_lib",
    srcs = [
        "fake_codec.cc",
    ],
    hdrs = ["fake_codec.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/generic_proxy:access_log_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
    ],
)

envoy_cc_test(
    name = "route_test",
    srcs = [
        "route_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":fake_codec_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/generic_proxy:route_lib",
        "//test/extensions/filters/network/generic_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:route_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "proxy_test",
    srcs = [
        "proxy_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":fake_codec_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/network/generic_proxy:proxy_lib",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:route_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":fake_codec_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/generic_proxy:config",
        "//source/extensions/filters/network/generic_proxy/router:config",
        "//source/extensions/tracers/zipkin:config",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:route_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = [
        "integration_test.cc",
    ],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        ":fake_codec_lib",
        "//envoy/event:timer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:thread_annotations",
        "//source/common/network:socket_option_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/generic_proxy:config",
        "//source/extensions/filters/network/generic_proxy:proxy_lib",
        "//source/extensions/filters/network/generic_proxy/router:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:route_mocks",
        "//test/integration:base_integration_test_lib",
        "//test/integration:common_extensions_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "match_test",
    srcs = [
        "match_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":fake_codec_lib",
        "//source/extensions/filters/network/generic_proxy:match_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "access_log_test",
    srcs = [
        "access_log_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":fake_codec_lib",
        "//source/extensions/filters/network/generic_proxy:access_log_lib",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_cc_test(
    name = "stream_test",
    srcs = [
        "stream_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/generic_proxy/interface:stream_interface",
    ],
)

envoy_cc_test(
    name = "tracing_test",
    srcs = [
        "tracing_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":fake_codec_lib",
        "//source/extensions/filters/network/generic_proxy:tracing_lib",
    ],
)

envoy_cc_test(
    name = "stats_test",
    srcs = [
        "stats_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/generic_proxy:stats_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

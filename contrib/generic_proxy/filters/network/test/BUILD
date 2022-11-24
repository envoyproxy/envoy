load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test_library(
    name = "fake_codec_lib",
    srcs = [
        "fake_codec.cc",
    ],
    hdrs = ["fake_codec.h"],
    deps = [
        "//contrib/generic_proxy/filters/network/source/interface:codec_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_test(
    name = "route_test",
    srcs = [
        "route_test.cc",
    ],
    deps = [
        ":fake_codec_lib",
        "//contrib/generic_proxy/filters/network/source:route_lib",
        "//contrib/generic_proxy/filters/network/test/mocks:filter_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:route_mocks",
        "//source/common/buffer:buffer_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "proxy_test",
    srcs = [
        "proxy_test.cc",
    ],
    deps = [
        ":fake_codec_lib",
        "//contrib/generic_proxy/filters/network/source:proxy_lib",
        "//contrib/generic_proxy/filters/network/test/mocks:codec_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:filter_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:route_mocks",
        "//source/common/buffer:buffer_lib",
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
    deps = [
        ":fake_codec_lib",
        "//contrib/generic_proxy/filters/network/source:config",
        "//contrib/generic_proxy/filters/network/test/mocks:codec_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:filter_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:route_mocks",
        "//source/common/buffer:buffer_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = [
        "integration_test.cc",
    ],
    deps = [
        ":fake_codec_lib",
        "//contrib/generic_proxy/filters/network/source:config",
        "//contrib/generic_proxy/filters/network/source:proxy_lib",
        "//contrib/generic_proxy/filters/network/test/mocks:codec_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:filter_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:route_mocks",
        "//envoy/event:timer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:thread_annotations",
        "//source/common/network:socket_option_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/integration:base_integration_test_lib",
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
    deps = [
        ":fake_codec_lib",
        "//contrib/generic_proxy/filters/network/source:match_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

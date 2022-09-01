load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
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
        "//source/extensions/filters/network/meta_protocol_proxy/interface:codec_interface",
    ],
)

envoy_extension_cc_test(
    name = "route_test",
    srcs = [
        "route_test.cc",
    ],
    extension_names = ["envoy.filters.network.meta_protocol_proxy"],
    deps = [
        ":fake_codec_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/meta_protocol_proxy:route_lib",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:route_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "proxy_test",
    srcs = [
        "proxy_test.cc",
    ],
    extension_names = ["envoy.filters.network.meta_protocol_proxy"],
    deps = [
        ":fake_codec_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/meta_protocol_proxy:proxy_lib",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:route_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = [
        "integration_test.cc",
    ],
    extension_names = ["envoy.filters.network.meta_protocol_proxy"],
    deps = [
        ":fake_codec_lib",
        "//envoy/event:timer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:thread_annotations",
        "//source/common/network:socket_option_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/meta_protocol_proxy:config",
        "//source/extensions/filters/network/meta_protocol_proxy:proxy_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:route_mocks",
        "//test/integration:base_integration_test_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "match_test",
    srcs = [
        "match_test.cc",
    ],
    extension_names = ["envoy.filters.network.meta_protocol_proxy"],
    deps = [
        ":fake_codec_lib",
        "//source/extensions/filters/network/meta_protocol_proxy:match_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

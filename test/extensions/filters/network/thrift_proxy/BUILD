licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

envoy_package()

envoy_extension_cc_mock(
    name = "mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        "//source/extensions/filters/network/thrift_proxy:transport_lib",
        "//test/test_common:printers_lib",
    ],
)

envoy_extension_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_lib",
    ],
)

envoy_extension_cc_test(
    name = "binary_protocol_impl_test",
    srcs = ["binary_protocol_impl_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "buffer_helper_test",
    srcs = ["buffer_helper_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:buffer_helper_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "compact_protocol_impl_test",
    srcs = ["compact_protocol_impl_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        "//source/extensions/filters/network/thrift_proxy:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "decoder_test",
    srcs = ["decoder_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:decoder_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":utility_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/thrift_proxy:filter_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_extension_cc_test(
    name = "framed_transport_impl_test",
    srcs = ["framed_transport_impl_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:transport_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "protocol_impl_test",
    srcs = ["protocol_impl_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "transport_impl_test",
    srcs = ["transport_impl_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:transport_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "unframed_transport_impl_test",
    srcs = ["unframed_transport_impl_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:transport_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "filter_integration_test",
    srcs = ["filter_integration_test.cc"],
    data = [
        "//test/extensions/filters/network/thrift_proxy/driver:generate_fixture",
    ],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/filters/network/thrift_proxy:config",
        "//source/extensions/filters/network/thrift_proxy:filter_lib",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:printers_lib",
    ],
)

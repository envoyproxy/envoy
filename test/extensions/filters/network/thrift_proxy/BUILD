licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_cc_mock(
    name = "mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/filters/network/thrift_proxy:transport_lib",
        "//test/test_common:printers_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_lib",
    ],
)

envoy_extension_cc_test(
    name = "binary_protocol_test",
    srcs = ["binary_protocol_test.cc"],
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
    name = "compact_protocol_test",
    srcs = ["compact_protocol_test.cc"],
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
    name = "protocol_test",
    srcs = ["protocol_test.cc"],
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
    name = "transport_test",
    srcs = ["transport_test.cc"],
    extension_name = "envoy.filters.network.thrift_proxy",
    deps = [
        ":mocks",
        ":utility_lib",
        "//source/extensions/filters/network/thrift_proxy:transport_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

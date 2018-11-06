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
    name = "mocks_lib",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/filters/network/dubbo_proxy:deserializer_interface",
        "//source/extensions/filters/network/dubbo_proxy:protocol_interface",
        "//test/test_common:printers_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_extension_cc_test(
    name = "hessian_utils_test",
    srcs = ["hessian_utils_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_utils_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "dubbo_protocol_impl_test",
    srcs = ["dubbo_protocol_impl_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:dubbo_protocol_impl_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "hessian_deserializer_impl_test",
    srcs = ["hessian_deserializer_impl_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":mocks_lib",
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:hessian_deserializer_impl_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        "//source/extensions/filters/network/dubbo_proxy:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_name = "envoy.filters.network.dubbo_proxy",
    deps = [
        ":utility_lib",
        "//source/extensions/filters/network/dubbo_proxy:filter_lib",
        "//test/mocks/server:server_mocks",
    ],
)

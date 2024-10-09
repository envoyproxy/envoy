load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "pass_through_filter_test",
    srcs = ["pass_through_filter_test.cc"],
    extension_names = ["envoy.filters.udp_listener.udp_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/udp/udp_proxy/session_filters:pass_through_filter_lib",
        "//test/extensions/filters/udp/udp_proxy:mocks",
    ],
)

envoy_proto_library(
    name = "drainer_filter_proto",
    srcs = ["drainer_filter.proto"],
)

envoy_cc_test_library(
    name = "drainer_filter_config_lib",
    hdrs = ["drainer_filter.h"],
    deps = [
        ":drainer_filter_proto_cc_proto",
        "//envoy/registry",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters:factory_base_lib",
        "//test/test_common:utility_lib",
    ],
    alwayslink = 1,
)

envoy_proto_library(
    name = "buffer_filter_proto",
    srcs = ["buffer_filter.proto"],
)

envoy_cc_test_library(
    name = "buffer_filter_config_lib",
    hdrs = ["buffer_filter.h"],
    deps = [
        ":buffer_filter_proto_cc_proto",
        "//envoy/registry",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters:factory_base_lib",
        "//test/test_common:utility_lib",
    ],
    alwayslink = 1,
)

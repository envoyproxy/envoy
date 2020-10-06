load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "http1_bridge_filter_test",
    srcs = ["http1_bridge_filter_test.cc"],
    extension_name = "envoy.filters.http.grpc_http1_bridge",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_http1_bridge:http1_bridge_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.grpc_http1_bridge",
    deps = [
        "//source/extensions/filters/http/grpc_http1_bridge:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

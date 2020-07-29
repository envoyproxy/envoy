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
    name = "reverse_bridge_test",
    srcs = ["reverse_bridge_test.cc"],
    extension_name = "envoy.filters.http.grpc_http1_reverse_bridge",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_http1_reverse_bridge:filter_lib",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "reverse_bridge_integration_test",
    srcs = ["reverse_bridge_integration_test.cc"],
    extension_name = "envoy.filters.http.grpc_http1_reverse_bridge",
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_http1_reverse_bridge:config",
        "//source/extensions/filters/http/grpc_http1_reverse_bridge:filter_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.grpc_http1_reverse_bridge",
    deps = [
        "//source/extensions/filters/http/grpc_http1_reverse_bridge:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3:pkg_cc_proto",
    ],
)

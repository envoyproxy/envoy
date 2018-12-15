licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "grpc_shim_test",
    srcs = ["grpc_shim_test.cc"],
    extension_name = "envoy.filters.http.grpc_shim",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_shim:grpc_shim_lib",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "grpc_shim_integration_test",
    srcs = ["grpc_shim_integration_test.cc"],
    extension_name = "envoy.filters.http.grpc_shim",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_shim:config",
        "//source/extensions/filters/http/grpc_shim:grpc_shim_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

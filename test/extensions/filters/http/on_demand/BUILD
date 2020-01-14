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
    name = "on_demand_filter_test",
    srcs = ["on_demand_filter_test.cc"],
    extension_name = "envoy.filters.http.on_demand",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/on_demand:on_demand_update_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)

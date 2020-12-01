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
    name = "response_map_filter_test",
    srcs = ["response_map_filter_test.cc"],
    extension_name = "envoy.filters.http.response_map",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/response_map:config",
        "//source/extensions/filters/http/response_map:response_map_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

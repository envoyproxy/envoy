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
    name = "ip_tagging_filter_test",
    srcs = ["ip_tagging_filter_test.cc"],
    data = [
        "//test/extensions/filters/http/ip_tagging/test_data:ip_tagging_files",
    ],
    extension_names = ["envoy.filters.http.ip_tagging"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/extensions/filters/http/ip_tagging:config",
        "//source/extensions/filters/http/ip_tagging:ip_tagging_filter_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/ip_tagging/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ip_tagging_integration_test",
    size = "large",
    srcs = ["ip_tagging_integration_test.cc"],
    extension_names = ["envoy.filters.http.ip_tagging"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/ip_tagging:config",
        "//test/integration:http_integration_lib",
    ],
)

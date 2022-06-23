load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_extension_cc_test(
    name = "cache_policy_test",
    srcs = ["cache_policy_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/http:header_map_interface",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/extensions/cache/cache_policy:cache_policy_lib",
        "//source/extensions/filters/http/cache:cache_custom_headers",
        "//source/extensions/filters/http/cache:cache_entry_utils_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/cache/simple_http_cache/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/cache/v3:pkg_cc_proto",
    ],
)

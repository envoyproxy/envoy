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
    name = "priority_load_shed_filter_test",
    srcs = ["priority_load_shed_filter_test.cc"],
    extension_names = ["envoy.filters.http.priority_load_shed"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/priority_load_shed:config",
        "//source/extensions/filters/http/priority_load_shed:filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/priority_load_shed/v3:pkg_cc_proto",
    ],
)

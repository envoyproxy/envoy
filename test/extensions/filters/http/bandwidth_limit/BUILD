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
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.bandwidth_limit"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/bandwidth_limit:bandwidth_limit_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_limit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.bandwidth_limit"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/bandwidth_limit:config",
        "//test/mocks/server:server_mocks",
    ],
)

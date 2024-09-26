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
    name = "set_metadata_filter_test",
    srcs = [
        "set_metadata_filter_test.cc",
    ],
    extension_names = ["envoy.filters.http.set_metadata"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/set_metadata:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.set_metadata"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/set_metadata:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/set_metadata/v3:pkg_cc_proto",
    ],
)

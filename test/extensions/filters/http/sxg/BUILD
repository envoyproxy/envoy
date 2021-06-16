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
    extension_name = "envoy.filters.http.sxg",
    deps = [
        "//source/common/secret:secret_manager_impl_lib",
        "//source/extensions/filters/http/sxg:config",
        "//source/extensions/filters/http/sxg:sxg_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)

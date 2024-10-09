load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/sxg/filters/http/source:config",
        "//contrib/sxg/filters/http/source:sxg_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//contrib/envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/sxg/filters/http/source:config",
        "//contrib/sxg/filters/http/source:sxg_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//contrib/envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)

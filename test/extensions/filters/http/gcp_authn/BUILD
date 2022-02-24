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

# TODO(tyxia) Add test
envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.gcp_authn"],
    deps = [
        "//source/extensions/filters/http/gcp_authn:filter_config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

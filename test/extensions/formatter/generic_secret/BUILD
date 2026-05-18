load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "generic_secret_test",
    srcs = ["generic_secret_test.cc"],
    extension_names = ["envoy.formatter.generic_secret"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/secret:secret_provider_impl_lib",
        "//source/extensions/formatter/generic_secret:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/formatter/generic_secret/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "generic_secret_integration_test",
    size = "large",
    srcs = ["generic_secret_integration_test.cc"],
    rbe_pool = "6gig",
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/formatter/generic_secret:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/formatter/generic_secret/v3:pkg_cc_proto",
    ],
)

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "@envoy//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "assertion_filter_test",
    srcs = ["assertion_filter_test.cc"],
    extension_names = ["envoy.filters.http.assertion"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/filters/http/assertion:config",
        "//library/common/extensions/filters/http/assertion:filter_cc_proto",
        "@envoy//test/mocks/http:http_mocks",
        "@envoy//test/mocks/server:factory_context_mocks",
        "@envoy//test/test_common:utility_lib",
    ],
)

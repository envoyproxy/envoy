licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "gradient_test",
    srcs = ["gradient_test.cc"],
    extension_name = "envoy.filters.http.adaptive_concurrency_limit",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http/adaptive_concurrency_limit/common:common_lib",
        "//source/extensions/filters/http/adaptive_concurrency_limit/limit:gradient_lib",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/config/filter/http/adaptive_concurrency_limit/v2alpha:adaptive_concurrency_limit_cc",
    ],
)

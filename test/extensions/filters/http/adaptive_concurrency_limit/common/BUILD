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
    name = "common_test",
    srcs = ["common_test.cc"],
    extension_name = "envoy.filters.http.adaptive_concurrency_limit",
    deps = [
        "//source/extensions/filters/http/adaptive_concurrency_limit/common:common_lib",
    ],
)

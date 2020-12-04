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
    name = "bandwidth_limit_config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.bandwidth_limit",
    deps = [
        "//source/extensions/filters/http/bandwidth_limit:config",
        "//test/mocks/server:server_mocks",
    ],
)

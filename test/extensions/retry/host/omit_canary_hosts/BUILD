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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.retry_host_predicates.omit_canary_hosts",
    deps = [
        "//source/extensions/retry/host/omit_canary_hosts:config",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

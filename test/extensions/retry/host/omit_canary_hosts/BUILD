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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "community.retry_host_predicates.canary_host_omitting",
    deps = [
        "//source/extensions/retry/host/omit_canary_hosts:config",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
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
    extension_name = "envoy.retry_host_predicates.omit_hosts",
    deps = [
        "//source/extensions/retry/host:well_known_names",
        "//source/extensions/retry/host/omit_hosts:config",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/retry/omit_hosts:pkg_cc_proto",
    ],
)

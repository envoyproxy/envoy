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
    extension_name = "envoy.retry_host_predicates.omit_host_metadata",
    deps = [
        "//source/extensions/retry/host:well_known_names",
        "//source/extensions/retry/host/omit_host_metadata:config",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/retry/host/omit_host_metadata/v3:pkg_cc_proto",
    ],
)

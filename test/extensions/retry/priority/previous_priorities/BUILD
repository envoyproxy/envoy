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
    extension_name = "envoy.retry_priorities.previous_priorities",
    deps = [
        "//source/extensions/retry/priority:well_known_names",
        "//source/extensions/retry/priority/previous_priorities:config",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

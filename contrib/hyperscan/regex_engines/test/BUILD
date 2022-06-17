load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/hyperscan/regex_engines/source:config",
        "//test/integration:integration_lib",
        "@envoy_api//contrib/envoy/extensions/regex_engines/hyperscan/v3alpha:pkg_cc_proto",
    ],
)

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "@envoy//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "network_configuration_retry_options_test",
    srcs = ["predicate_test.cc"],
    extension_names = ["envoy.retry.options.network_configuration"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/retry/options/network_configuration:config",
        "//library/common/network:connectivity_manager_lib",
        "@envoy//source/common/upstream:retry_factory_lib",
        "@envoy//test/mocks/server:factory_context_mocks",
        "@envoy//test/mocks/stream_info:stream_info_mocks",
    ],
)

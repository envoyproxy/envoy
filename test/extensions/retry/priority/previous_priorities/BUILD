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
    extension_names = ["envoy.retry_priorities.previous_priorities"],
    rbe_pool = "2core",
    deps = [
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/retry/priority/previous_priorities:config",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/extensions/retry/priority/previous_priorities/v3:pkg_cc_proto",
    ],
)

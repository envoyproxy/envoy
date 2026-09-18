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
    name = "adaptive_lifo_queue_policy_test",
    srcs = ["adaptive_lifo_queue_policy_test.cc"],
    extension_names = ["envoy.queue_policy.adaptive_lifo"],
    deps = [
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/queue_policy/adaptive_lifo:adaptive_lifo_queue_policy_impl",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

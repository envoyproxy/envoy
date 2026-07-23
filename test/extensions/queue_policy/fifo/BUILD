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
    name = "fifo_queue_policy_test",
    srcs = ["fifo_queue_policy_test.cc"],
    extension_names = ["envoy.queue_policy.fifo"],
    deps = [
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/queue_policy/fifo:fifo_queue_policy_impl",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

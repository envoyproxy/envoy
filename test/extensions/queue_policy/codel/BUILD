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
    name = "codel_queue_policy_test",
    srcs = ["codel_queue_policy_test.cc"],
    extension_names = [
        "envoy.queue_policy.codel",
        "envoy.queue_policy.fifo",
    ],
    deps = [
        "//source/common/conn_pool:conn_pool_base_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/queue_policy:queue_policy_base_lib",
        "//source/extensions/queue_policy/codel:codel_queue_policy_impl",
        "//source/extensions/queue_policy/fifo:fifo_queue_policy_impl",
        "//test/test_common:simulated_time_system_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@envoy_api//envoy/extensions/queue_policy/fifo/v3:pkg_cc_proto",
    ],
)

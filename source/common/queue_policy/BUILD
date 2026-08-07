load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "queue_policy_base_lib",
    hdrs = [
        "fifo_queue_policy.h",
        "queue_policy_base.h",
    ],
    deps = [
        "//envoy/common:conn_pool_interface",
        "//envoy/common:time_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status:statusor",
    ],
)

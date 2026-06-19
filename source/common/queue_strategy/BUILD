load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "queue_strategy_base_lib",
    hdrs = ["queue_strategy_base.h"],
    deps = [
        "//envoy/common:conn_pool_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/common:linked_object",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:variant",
    ],
)

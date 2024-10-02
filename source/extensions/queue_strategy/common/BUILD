load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "queue_strategy_base_lib",
    hdrs = ["queue_strategy_base.h"],
    deps = [
        "//envoy/common:conn_pool_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:linked_object",
    ],
)

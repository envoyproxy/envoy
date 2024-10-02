load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "fifo_queue_strategy_impl",
    hdrs = ["fifo_queue_strategy.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/queue_strategy/common:queue_strategy_base_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/config:utility_lib",
    ],
)

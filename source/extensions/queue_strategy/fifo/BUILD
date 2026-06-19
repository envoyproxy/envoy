load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "fifo_queue_strategy_impl",
    srcs = ["fifo_queue_strategy.cc"],
    hdrs = ["fifo_queue_strategy.h"],
    deps = [
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/conn_pool:conn_pool_base_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/queue_strategy:queue_strategy_base_lib",
        "@envoy_api//envoy/extensions/queue_strategy/fifo/v3:pkg_cc_proto",
    ],
)

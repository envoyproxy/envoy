licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "envoy_quic_alarm_lib",
    srcs = ["envoy_quic_alarm.cc"],
    hdrs = ["envoy_quic_alarm.h"],
    external_deps = ["quiche_quic_platform"],
    deps = [
        "//include/envoy/event:timer_interface",
        "@com_googlesource_quiche//:quic_core_alarm_interface_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_alarm_factory_lib",
    srcs = ["envoy_quic_alarm_factory.cc"],
    hdrs = ["envoy_quic_alarm_factory.h"],
    external_deps = ["quiche_quic_platform"],
    deps = [
        ":envoy_quic_alarm_lib",
        "@com_googlesource_quiche//:quic_core_alarm_factory_interface_lib",
        "@com_googlesource_quiche//:quic_core_arena_scoped_ptr_lib",
        "@com_googlesource_quiche//:quic_core_one_block_arena_lib",
    ],
)

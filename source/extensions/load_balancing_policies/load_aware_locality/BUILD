load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":load_aware_locality_lb_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/load_balancing_policies/load_aware_locality/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "load_aware_locality_lb_lib",
    srcs = ["load_aware_locality_lb.cc"],
    hdrs = ["load_aware_locality_lb.h"],
    deps = [
        "//envoy/common:random_generator_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:upstream_includes",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

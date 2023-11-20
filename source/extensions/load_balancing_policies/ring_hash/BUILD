load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ring_hash_lb_lib",
    srcs = ["ring_hash_lb.cc"],
    hdrs = ["ring_hash_lb.h"],
    external_deps = [
        "abseil_inlined_vector",
    ],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:thread_aware_lb_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/ring_hash/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    extra_visibility = [
        # previously considered core code.
        "//test:__subpackages__",
    ],
    deps = [
        ":ring_hash_lb_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@envoy_api//envoy/extensions/load_balancing_policies/ring_hash/v3:pkg_cc_proto",
    ],
)

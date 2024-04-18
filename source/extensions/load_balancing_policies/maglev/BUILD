load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "maglev_lb_lib",
    srcs = ["maglev_lb.cc"],
    hdrs = ["maglev_lb.h"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/common/common:bit_array_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/upstream:thread_aware_lb_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/maglev/v3:pkg_cc_proto",
    ],
)

# This library target is for testing especially x86 coverage
# which will use the compact representation unless we force the
# original implementation.
envoy_cc_library(
    name = "maglev_lb_force_original_impl_lib",
    srcs = ["maglev_lb.cc"],
    hdrs = ["maglev_lb.h"],
    copts = ["-DMAGLEV_LB_FORCE_ORIGINAL_IMPL"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/common/common:bit_array_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/upstream:thread_aware_lb_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/maglev/v3:pkg_cc_proto",
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
        ":maglev_lb_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@envoy_api//envoy/extensions/load_balancing_policies/maglev/v3:pkg_cc_proto",
    ],
)

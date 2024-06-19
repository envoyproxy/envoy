load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "subset_lb_lib",
    srcs = ["subset_lb.cc"],
    hdrs = ["subset_lb.h"],
    deps = [
        ":subset_lb_config_lib",
        "//envoy/runtime:runtime_interface",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/subset/v3:pkg_cc_proto",
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
        ":subset_lb_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/common/upstream:load_balancer_factory_base_lib",
    ],
)

envoy_cc_extension(
    name = "subset_lb_config_lib",
    srcs = ["subset_lb_config.cc"],
    hdrs = ["subset_lb_config.h"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/config:utility_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/common/upstream:upstream_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/common/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/subset/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "factory_base",
    hdrs = ["factory_base.h"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/common/upstream:load_balancer_factory_base_lib",
    ],
)

envoy_cc_library(
    name = "thread_aware_lb_lib",
    srcs = ["thread_aware_lb_impl.cc"],
    hdrs = ["thread_aware_lb_impl.h"],
    external_deps = ["abseil_synchronization"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/upstream:load_balancer_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)
